#pragma once

#include "finevk/core/types.hpp"
#include "finevk/device/logical_device.hpp"
#include "finevk/device/command.hpp"
#include "finevk/high/texture.hpp"
#include "finevk/high/mesh.hpp"
#include <memory>
#include <string>

namespace finevk {

/**
 * Asynchronous asset loading system
 *
 * Loads textures and meshes from disk on background worker threads.
 * Never returns null - uses sentinel objects for pending/error states.
 *
 * Usage:
 *   auto loader = AssetLoader::create(device, commandPool);
 *   TextureRef tex = loader->loadTexture("floor.png");  // Returns immediately
 *
 *   // In game loop
 *   loader->update();  // Process GPU uploads (time-budgeted)
 *
 *   // Use texture directly - no null checks needed
 *   material->setTexture(0, tex);  // Shows: pending -> real -> error
 */
class AssetLoader {
public:
    /**
     * Create an AssetLoader instance (worker threads NOT started)
     *
     * Call start() to begin background loading.
     *
     * @param device The logical device for creating GPU resources
     * @param commandPool Command pool for GPU uploads
     * @param numWorkers Number of background worker threads (minimum: 1)
     * @return Unique pointer to the AssetLoader
     */
    static std::unique_ptr<AssetLoader> create(
        LogicalDevice* device,
        CommandPool* commandPool,
        uint32_t numWorkers = 1
    );

    virtual ~AssetLoader() = default;

    // --- Lifecycle Management ---

    /**
     * Start worker threads
     *
     * Must be called before loading assets. Can be called from any thread.
     * Safe to call multiple times (no-op if already started).
     */
    virtual void start() = 0;

    /**
     * Stop worker threads gracefully
     *
     * Waits for current work to complete. Safe to call even if not started.
     * Automatically called by destructor if not explicitly stopped.
     */
    virtual void stop() = 0;

    /**
     * Check if worker threads are running
     */
    virtual bool isRunning() const = 0;

    // --- Asset Loading ---

    /**
     * Load a texture from disk (async)
     *
     * Returns immediately with a TextureRef that initially points to the
     * pending sentinel texture. Once loaded and uploaded to GPU, the shared_ptr
     * is updated to point to the real texture (or error sentinel on failure).
     *
     * @param path Path to texture file (relative or absolute)
     * @param generateMipmaps Whether to generate mipmaps
     * @param srgb Whether to use sRGB color space
     * @return TextureRef (NEVER NULL - sentinel until loaded)
     * @throws std::runtime_error if called before start()
     */
    virtual TextureRef loadTexture(
        const std::string& path,
        bool generateMipmaps = true,
        bool srgb = true
    ) = 0;

    /**
     * Load a mesh from disk (async)
     *
     * Returns immediately with a MeshRef that initially points to the
     * pending sentinel mesh. Once loaded and uploaded to GPU, the shared_ptr
     * is updated to point to the real mesh (or error sentinel on failure).
     *
     * @param path Path to mesh file (relative or absolute)
     * @param attributes Vertex attributes to load
     * @return MeshRef (NEVER NULL - sentinel until loaded)
     * @throws std::runtime_error if called before start()
     */
    virtual MeshRef loadMesh(
        const std::string& path,
        VertexAttribute attributes = VertexAttribute::Position |
                                     VertexAttribute::Normal |
                                     VertexAttribute::TexCoord
    ) = 0;

    // --- Per-Frame Update ---

    /**
     * Process pending GPU uploads (call once per frame on main thread)
     *
     * Uploads staged assets to GPU with time budgeting to avoid frame drops.
     *
     * @param timeBudget Maximum time to spend on uploads (seconds, default: 2ms)
     * @return Number of assets uploaded this frame
     */
    virtual size_t update(float timeBudget = 0.002f) = 0;

    // --- Status Queries ---

    /**
     * Check if an asset is fully loaded and ready
     *
     * @param path Asset path
     * @return True if loaded and uploaded to GPU
     */
    virtual bool isReady(const std::string& path) const = 0;

    /**
     * Check if an asset failed to load
     *
     * @param path Asset path
     * @return True if loading failed
     */
    virtual bool isFailed(const std::string& path) const = 0;

    /**
     * Get loading progress for an asset
     *
     * @param path Asset path
     * @return Progress [0.0 = not started, 1.0 = complete]
     */
    virtual float getProgress(const std::string& path) const = 0;

    /**
     * Get error message for failed asset
     *
     * @param path Asset path
     * @return Error message (empty if not failed)
     */
    virtual std::string getError(const std::string& path) const = 0;

    // --- Sentinel Object Access ---

    /**
     * Get the pending sentinel texture
     * Debug: Black/yellow checkerboard, Release: Gray
     */
    virtual TextureRef pendingTexture() const = 0;

    /**
     * Get the error sentinel texture
     * Magenta checkerboard (always visible)
     */
    virtual TextureRef errorTexture() const = 0;

    /**
     * Get the pending sentinel mesh
     * Wireframe cube
     */
    virtual MeshRef pendingMesh() const = 0;

    /**
     * Get the error sentinel mesh
     * Magenta solid cube
     */
    virtual MeshRef errorMesh() const = 0;

    // --- Utility ---

    /**
     * Get total number of assets in cache
     */
    virtual size_t getCacheSize() const = 0;

    /**
     * Get number of assets currently loading
     */
    virtual size_t getPendingCount() const = 0;

    /**
     * Get number of worker threads
     */
    virtual uint32_t getWorkerCount() const = 0;

protected:
    AssetLoader() = default;
    AssetLoader(const AssetLoader&) = delete;
    AssetLoader& operator=(const AssetLoader&) = delete;
};

} // namespace finevk
