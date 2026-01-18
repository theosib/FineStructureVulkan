#include "finevk/engine/asset_loader.hpp"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <unordered_map>
#include <atomic>
#include <chrono>
#include <fstream>

#include <stb_image.h>
#include <tiny_obj_loader.h>

namespace finevk {

// --- Internal Structures ---

enum class AssetState {
    Pending,      // Queued for loading
    Loading,      // Being loaded by worker
    Staging,      // CPU data ready, waiting for GPU upload
    Ready,        // Fully loaded and on GPU
    Failed        // Failed to load
};

struct AssetMetadata {
    AssetState state = AssetState::Pending;
    float progress = 0.0f;
    std::string error;

    // For textures
    bool generateMipmaps = true;
    bool srgb = true;

    // For meshes
    VertexAttribute attributes = VertexAttribute::Position;
};

// CPU-side staging data
struct TextureStaging {
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    int channels = 0;
    bool generateMipmaps = true;
    bool srgb = true;

    ~TextureStaging() {
        if (pixels) {
            stbi_image_free(pixels);
        }
    }
};

struct MeshStaging {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    VertexAttribute attributes;
};

// Work item for worker threads
struct WorkItem {
    enum class Type { Texture, Mesh };

    Type type;
    std::string path;
    AssetMetadata metadata;
};

// Cache entry
struct CacheEntry {
    AssetMetadata metadata;

    // Shared refs (always valid - sentinel or real)
    TextureRef textureRef;
    MeshRef meshRef;

    // Staging data (only valid during Staging state)
    std::unique_ptr<TextureStaging> textureStaging;
    std::unique_ptr<MeshStaging> meshStaging;
};

// --- AssetLoader Implementation ---

class AssetLoaderImpl : public AssetLoader {
public:
    AssetLoaderImpl(LogicalDevice* device, CommandPool* commandPool, uint32_t numWorkers)
        : device_(device)
        , commandPool_(commandPool)
        , numWorkers_(std::max(1u, numWorkers))  // Minimum 1 worker
        , running_(false)
        , shutdown_(false)
    {
        createSentinelAssets();
        // NOTE: Workers NOT started here - call start() explicitly
    }

    ~AssetLoaderImpl() override {
        stop();  // Safe even if never started
    }

    // --- Lifecycle Management ---

    void start() override {
        std::lock_guard<std::mutex> lock(lifeCycleMutex_);

        if (running_) {
            return;  // Already started
        }

        shutdown_ = false;
        startWorkers();
        running_ = true;
    }

    void stop() override {
        std::lock_guard<std::mutex> lock(lifeCycleMutex_);

        if (!running_) {
            return;  // Not started or already stopped
        }

        shutdownWorkers();
        running_ = false;
    }

    bool isRunning() const override {
        std::lock_guard<std::mutex> lock(lifeCycleMutex_);
        return running_;
    }

    // --- Asset Loading ---

    TextureRef loadTexture(const std::string& path, bool generateMipmaps, bool srgb) override {
        // Check if service is running
        {
            std::lock_guard<std::mutex> lifeLock(lifeCycleMutex_);
            if (!running_) {
                throw std::runtime_error("AssetLoader::loadTexture called before start()");
            }
        }

        std::lock_guard<std::mutex> lock(cacheMutex_);

        auto it = cache_.find(path);
        if (it != cache_.end()) {
            return it->second.textureRef;
        }

        // Create new cache entry using emplace
        CacheEntry entry;
        entry.metadata.state = AssetState::Pending;
        entry.metadata.generateMipmaps = generateMipmaps;
        entry.metadata.srgb = srgb;
        entry.textureRef = pendingTexture_;

        auto inserted = cache_.emplace(path, std::move(entry));

        // Queue work
        WorkItem work;
        work.type = WorkItem::Type::Texture;
        work.path = path;
        work.metadata = inserted.first->second.metadata;

        {
            std::lock_guard<std::mutex> queueLock(queueMutex_);
            workQueue_.push(work);
        }
        queueCV_.notify_one();

        return inserted.first->second.textureRef;
    }

    MeshRef loadMesh(const std::string& path, VertexAttribute attributes) override {
        // Check if service is running
        {
            std::lock_guard<std::mutex> lifeLock(lifeCycleMutex_);
            if (!running_) {
                throw std::runtime_error("AssetLoader::loadMesh called before start()");
            }
        }

        std::lock_guard<std::mutex> lock(cacheMutex_);

        auto it = cache_.find(path);
        if (it != cache_.end()) {
            return it->second.meshRef;
        }

        // Create new cache entry using emplace
        CacheEntry entry;
        entry.metadata.state = AssetState::Pending;
        entry.metadata.attributes = attributes;
        entry.meshRef = pendingMesh_;

        auto inserted = cache_.emplace(path, std::move(entry));

        // Queue work
        WorkItem work;
        work.type = WorkItem::Type::Mesh;
        work.path = path;
        work.metadata = inserted.first->second.metadata;

        {
            std::lock_guard<std::mutex> queueLock(queueMutex_);
            workQueue_.push(work);
        }
        queueCV_.notify_one();

        return inserted.first->second.meshRef;
    }

    // --- Per-Frame Update ---

    size_t update(float timeBudget) override {
        auto startTime = std::chrono::high_resolution_clock::now();
        size_t uploadCount = 0;

        std::lock_guard<std::mutex> lock(cacheMutex_);

        for (auto& pair : cache_) {
            // Check time budget
            auto currentTime = std::chrono::high_resolution_clock::now();
            float elapsed = std::chrono::duration<float>(currentTime - startTime).count();
            if (elapsed >= timeBudget) {
                break;
            }

            CacheEntry& entry = pair.second;

            if (entry.metadata.state == AssetState::Staging) {
                if (entry.textureStaging) {
                    uploadTexture(pair.first, entry);
                    uploadCount++;
                } else if (entry.meshStaging) {
                    uploadMesh(pair.first, entry);
                    uploadCount++;
                }
            }
        }

        return uploadCount;
    }

    // --- Status Queries ---

    bool isReady(const std::string& path) const override {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        auto it = cache_.find(path);
        return it != cache_.end() && it->second.metadata.state == AssetState::Ready;
    }

    bool isFailed(const std::string& path) const override {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        auto it = cache_.find(path);
        return it != cache_.end() && it->second.metadata.state == AssetState::Failed;
    }

    float getProgress(const std::string& path) const override {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        auto it = cache_.find(path);
        return it != cache_.end() ? it->second.metadata.progress : 0.0f;
    }

    std::string getError(const std::string& path) const override {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        auto it = cache_.find(path);
        return it != cache_.end() ? it->second.metadata.error : "";
    }

    // --- Sentinel Object Access ---

    TextureRef pendingTexture() const override { return pendingTexture_; }
    TextureRef errorTexture() const override { return errorTexture_; }
    MeshRef pendingMesh() const override { return pendingMesh_; }
    MeshRef errorMesh() const override { return errorMesh_; }

    // --- Utility ---

    size_t getCacheSize() const override {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        return cache_.size();
    }

    size_t getPendingCount() const override {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        size_t count = 0;
        for (const auto& pair : cache_) {
            if (pair.second.metadata.state == AssetState::Pending ||
                pair.second.metadata.state == AssetState::Loading ||
                pair.second.metadata.state == AssetState::Staging) {
                count++;
            }
        }
        return count;
    }

    uint32_t getWorkerCount() const override {
        return numWorkers_;
    }

private:
    // --- Sentinel Asset Creation ---

    void createSentinelAssets() {
        createSentinelTextures();
        createSentinelMeshes();
    }

    void createSentinelTextures() {
        // Pending texture: Gray or checkerboard
        #ifdef NDEBUG
        // Release: Gray
        unsigned char grayPixels[4] = { 128, 128, 128, 255 };
        pendingTexture_ = Texture::fromMemory(device_, grayPixels, 1, 1, commandPool_, false, false);
        #else
        // Debug: Black/yellow checkerboard (4x4)
        unsigned char checkerPixels[16 * 4];
        for (int i = 0; i < 16; i++) {
            bool isYellow = ((i / 4) + (i % 4)) % 2 == 0;
            checkerPixels[i * 4 + 0] = isYellow ? 255 : 0;
            checkerPixels[i * 4 + 1] = isYellow ? 255 : 0;
            checkerPixels[i * 4 + 2] = isYellow ? 0 : 0;
            checkerPixels[i * 4 + 3] = 255;
        }
        pendingTexture_ = Texture::fromMemory(device_, checkerPixels, 4, 4, commandPool_, false, false);
        #endif

        // Error texture: Magenta checkerboard (4x4)
        unsigned char magentaPixels[16 * 4];
        for (int i = 0; i < 16; i++) {
            bool isMagenta = ((i / 4) + (i % 4)) % 2 == 0;
            magentaPixels[i * 4 + 0] = isMagenta ? 255 : 128;
            magentaPixels[i * 4 + 1] = 0;
            magentaPixels[i * 4 + 2] = isMagenta ? 255 : 128;
            magentaPixels[i * 4 + 3] = 255;
        }
        errorTexture_ = Texture::fromMemory(device_, magentaPixels, 4, 4, commandPool_, false, false);
    }

    void createSentinelMeshes() {
        // Pending mesh: Wireframe cube (simple triangles for now)
        auto builder1 = Mesh::create(device_);
        builder1.attributes(VertexAttribute::Position | VertexAttribute::Normal | VertexAttribute::TexCoord);

        // Simple cube vertices
        Vertex v0{{-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}};
        Vertex v1{{ 0.5f, -0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}};
        Vertex v2{{ 0.5f,  0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}};
        Vertex v3{{-0.5f,  0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}};
        Vertex v4{{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f}};
        Vertex v5{{ 0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f}};
        Vertex v6{{ 0.5f,  0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f}};
        Vertex v7{{-0.5f,  0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f}};

        // Front face
        builder1.addTriangle(v0, v1, v2).addTriangle(v2, v3, v0);
        // Back face
        builder1.addTriangle(v4, v6, v5).addTriangle(v4, v7, v6);

        pendingMesh_ = builder1.build(commandPool_);

        // Error mesh: Solid magenta cube
        auto builder2 = Mesh::create(device_);
        builder2.attributes(VertexAttribute::Position | VertexAttribute::Normal | VertexAttribute::TexCoord);

        // Front
        builder2.addTriangle(v0, v1, v2).addTriangle(v2, v3, v0);
        // Back
        builder2.addTriangle(v4, v6, v5).addTriangle(v4, v7, v6);
        // Bottom
        builder2.addTriangle(v0, v4, v5).addTriangle(v0, v5, v1);
        // Top
        builder2.addTriangle(v2, v6, v7).addTriangle(v2, v7, v3);
        // Left
        builder2.addTriangle(v0, v3, v7).addTriangle(v0, v7, v4);
        // Right
        builder2.addTriangle(v1, v5, v6).addTriangle(v1, v6, v2);

        errorMesh_ = builder2.build(commandPool_);
    }

    // --- Worker Thread Management ---

    void startWorkers() {
        for (uint32_t i = 0; i < numWorkers_; i++) {
            workers_.emplace_back(&AssetLoaderImpl::workerThread, this);
        }
    }

    void shutdownWorkers() {
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            shutdown_ = true;
        }
        queueCV_.notify_all();

        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        workers_.clear();
    }

    void workerThread() {
        while (true) {
            WorkItem work;

            // Wait for work
            {
                std::unique_lock<std::mutex> lock(queueMutex_);
                queueCV_.wait(lock, [this] {
                    return shutdown_ || !workQueue_.empty();
                });

                if (shutdown_) {
                    break;
                }

                work = workQueue_.front();
                workQueue_.pop();
            }

            // Process work
            if (work.type == WorkItem::Type::Texture) {
                loadTextureOnWorker(work.path, work.metadata);
            } else {
                loadMeshOnWorker(work.path, work.metadata);
            }
        }
    }

    // --- Asset Loading on Worker Threads ---

    void loadTextureOnWorker(const std::string& path, AssetMetadata metadata) {
        // Update state to Loading
        {
            std::lock_guard<std::mutex> lock(cacheMutex_);
            auto it = cache_.find(path);
            if (it != cache_.end()) {
                it->second.metadata.state = AssetState::Loading;
                it->second.metadata.progress = 0.1f;
            }
        }

        // Load image with stb_image
        auto staging = std::make_unique<TextureStaging>();
        staging->generateMipmaps = metadata.generateMipmaps;
        staging->srgb = metadata.srgb;

        int channels;
        staging->pixels = stbi_load(path.c_str(), &staging->width, &staging->height, &channels, 4);
        staging->channels = 4;  // Force RGBA

        if (!staging->pixels) {
            // Failed to load
            std::lock_guard<std::mutex> lock(cacheMutex_);
            auto it = cache_.find(path);
            if (it != cache_.end()) {
                it->second.metadata.state = AssetState::Failed;
                it->second.metadata.error = "Failed to load image: " + path;
                it->second.textureRef = errorTexture_;
            }
            return;
        }

        // Move to staging
        {
            std::lock_guard<std::mutex> lock(cacheMutex_);
            auto it = cache_.find(path);
            if (it != cache_.end()) {
                it->second.metadata.state = AssetState::Staging;
                it->second.metadata.progress = 0.8f;
                it->second.textureStaging = std::move(staging);
            }
        }
    }

    void loadMeshOnWorker(const std::string& path, AssetMetadata metadata) {
        // Update state to Loading
        {
            std::lock_guard<std::mutex> lock(cacheMutex_);
            auto it = cache_.find(path);
            if (it != cache_.end()) {
                it->second.metadata.state = AssetState::Loading;
                it->second.metadata.progress = 0.1f;
            }
        }

        // Load mesh with tinyobjloader
        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        std::string warn, err;

        bool success = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.c_str());

        if (!success) {
            // Failed to load
            std::lock_guard<std::mutex> lock(cacheMutex_);
            auto it = cache_.find(path);
            if (it != cache_.end()) {
                it->second.metadata.state = AssetState::Failed;
                it->second.metadata.error = "Failed to load mesh: " + err;
                it->second.meshRef = errorMesh_;
            }
            return;
        }

        // Build vertex/index data
        auto staging = std::make_unique<MeshStaging>();
        staging->attributes = metadata.attributes;

        for (const auto& shape : shapes) {
            for (const auto& index : shape.mesh.indices) {
                Vertex vertex{};

                vertex.position = {
                    attrib.vertices[3 * index.vertex_index + 0],
                    attrib.vertices[3 * index.vertex_index + 1],
                    attrib.vertices[3 * index.vertex_index + 2]
                };

                if (index.normal_index >= 0 && (metadata.attributes & VertexAttribute::Normal)) {
                    vertex.normal = {
                        attrib.normals[3 * index.normal_index + 0],
                        attrib.normals[3 * index.normal_index + 1],
                        attrib.normals[3 * index.normal_index + 2]
                    };
                }

                if (index.texcoord_index >= 0 && (metadata.attributes & VertexAttribute::TexCoord)) {
                    vertex.texCoord = {
                        attrib.texcoords[2 * index.texcoord_index + 0],
                        1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
                    };
                }

                staging->vertices.push_back(vertex);
                staging->indices.push_back(static_cast<uint32_t>(staging->indices.size()));
            }
        }

        // Move to staging
        {
            std::lock_guard<std::mutex> lock(cacheMutex_);
            auto it = cache_.find(path);
            if (it != cache_.end()) {
                it->second.metadata.state = AssetState::Staging;
                it->second.metadata.progress = 0.8f;
                it->second.meshStaging = std::move(staging);
            }
        }
    }

    // --- GPU Upload on Main Thread ---

    void uploadTexture(const std::string&, CacheEntry& entry) {
        auto& staging = entry.textureStaging;

        try {
            auto texture = Texture::fromMemory(
                device_,
                staging->pixels,
                staging->width,
                staging->height,
                commandPool_,
                staging->generateMipmaps,
                staging->srgb
            );

            entry.textureRef = texture;
            entry.metadata.state = AssetState::Ready;
            entry.metadata.progress = 1.0f;
            entry.textureStaging.reset();

        } catch (const std::exception& e) {
            entry.metadata.state = AssetState::Failed;
            entry.metadata.error = std::string("GPU upload failed: ") + e.what();
            entry.textureRef = errorTexture_;
            entry.textureStaging.reset();
        }
    }

    void uploadMesh(const std::string&, CacheEntry& entry) {
        auto& staging = entry.meshStaging;

        try {
            auto builder = Mesh::create(device_);
            builder.attributes(staging->attributes);

            // Add all vertices and indices
            for (const auto& v : staging->vertices) {
                builder.addVertex(v);
            }
            builder.addIndices(staging->indices);

            auto mesh = builder.build(commandPool_);

            entry.meshRef = mesh;
            entry.metadata.state = AssetState::Ready;
            entry.metadata.progress = 1.0f;
            entry.meshStaging.reset();

        } catch (const std::exception& e) {
            entry.metadata.state = AssetState::Failed;
            entry.metadata.error = std::string("GPU upload failed: ") + e.what();
            entry.meshRef = errorMesh_;
            entry.meshStaging.reset();
        }
    }

    // --- Member Variables ---

    LogicalDevice* device_;
    CommandPool* commandPool_;
    uint32_t numWorkers_;

    // Sentinel assets
    TextureRef pendingTexture_;
    TextureRef errorTexture_;
    MeshRef pendingMesh_;
    MeshRef errorMesh_;

    // Cache
    mutable std::mutex cacheMutex_;
    std::unordered_map<std::string, CacheEntry> cache_;

    // Lifecycle
    mutable std::mutex lifeCycleMutex_;
    bool running_;

    // Worker threads
    std::vector<std::thread> workers_;
    std::queue<WorkItem> workQueue_;
    std::mutex queueMutex_;
    std::condition_variable queueCV_;
    std::atomic<bool> shutdown_;
};

// --- Factory Function ---

std::unique_ptr<AssetLoader> AssetLoader::create(
    LogicalDevice* device,
    CommandPool* commandPool,
    uint32_t numWorkers)
{
    return std::make_unique<AssetLoaderImpl>(device, commandPool, numWorkers);
}

} // namespace finevk
