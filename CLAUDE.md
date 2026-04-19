# FineVK

A high-performance Vulkan framework. `docs/STYLE_GUIDE.md` is the full design
philosophy; this file is the short form, focused on the rules agents miss most.

## The handle rule

**Never return a raw Vulkan resource handle from a public API.** Resource
types have lifetime; finevk owns that lifetime via wrapper classes. Raw
handles are accessible only via `.handle()` on the wrapper — Level 4 "raw
Vulkan" escape hatch, not the default access path.

**Always wrap (return as `Xxx&`, `XxxPtr`, `XxxRef`, or equivalent):**

| Vulkan type | finevk wrapper |
|---|---|
| `VkImage` | `Image` |
| `VkBuffer` | `Buffer` |
| `VkSemaphore` | `Semaphore` |
| `VkFence` | `Fence` |
| `VkCommandBuffer` | `CommandBuffer` |
| `VkCommandPool` | `CommandPool` |
| `VkPipeline` | `GraphicsPipeline` / `ComputePipeline` |
| `VkPipelineLayout` | `PipelineLayout` |
| `VkRenderPass` | `RenderPass` |
| `VkFramebuffer` | `Framebuffer` |
| `VkImageView` | `ImageView` |
| `VkSampler` | `Sampler` |
| `VkSwapchainKHR` | `SwapChain` |
| `VkShaderModule` | `ShaderModule` |
| `VkDescriptorSet`, `VkDescriptorPool` | `DescriptorSet`, `DescriptorPool` |
| `VkDeviceMemory` | managed inside `Image`/`Buffer`, never exposed |

**Always pass through as data (raw Vulkan types are fine; wrapping would
be pure overhead):**

- Extents, offsets, viewports, rects — `VkExtent2D`, `VkExtent3D`,
  `VkOffset2D`, `VkOffset3D`, `VkViewport`, `VkRect2D`
- Formats and sample counts — `VkFormat`, `VkSampleCountFlagBits`
- Usage and flag bitfields — `VkImageUsageFlags`, `VkBufferUsageFlags`,
  `VkAccessFlags`, `VkPipelineStageFlags`
- Layouts — `VkImageLayout`
- Pipeline config enums — `VkCompareOp`, `VkCullModeFlags`, `VkBlendFactor`,
  `VkPrimitiveTopology`, `VkPolygonMode`, etc.
- GLFW key codes

**Rule of thumb:** does this value identify an object with a Vulkan-tracked
lifetime? → wrap. Is it just a value describing configuration? → pass through.

**Recon before refactoring:** if you're about to change a public signature
that returns or takes a Vulkan type, first grep the downstream projects
for the signature. FineVK's consumers are listed below.

## The no-waitIdle rule

Avoid `vkDeviceWaitIdle` and `vkQueueWaitIdle` in any hot path. Use
`DeletionQueue` for deferred cleanup, per-resource fences for precise sync,
and `CommandPool::beginImmediate()` for one-shot operations that already
encapsulate the wait. Resize paths must use `DeletionQueue`, not `waitIdle`.

## The triple-overload rule

Every public method taking a finevk object parameter provides three forms:

```cpp
static Builder create(LogicalDevice* device);
static Builder create(LogicalDevice& device);
static Builder create(const LogicalDevicePtr& device);
```

No caller should ever need `.get()`. The raw-pointer form is the
implementation; the other two delegate inline.

## Downstream consumers

API breakage here costs work in these projects. Prefer non-breaking
extensions; deprecate before removing.

- `FineStructureVoxel` — `/Users/theosib/projects/FineStructure/FineStructureVoxel/`
- `finegui` — `/Users/theosib/projects/FineStructure/finegui/`

## Known pre-existing violations (work in progress)

- `SwapChain::images()` returns `std::vector<VkImage>` — violates the handle
  rule. `SwapChain::image(idx)` returning `Image&` added in the 2026-04-19
  accessor phase. New code should use the wrapper; the raw-handle accessor
  is retained for backward compatibility with existing call sites.

If you notice another violation while working, flag it in your report —
don't silently expand scope to fix it.

## Commit policy for agent work

Each task commits its task-scoped changes as a single commit before
reporting `status=done`. The commit hash goes in the task report. If the
working tree contains pre-existing uncommitted changes at task start,
STOP and return `status=blocked` — do not commit around or on top of
them.

## References

- `docs/STYLE_GUIDE.md` — full design philosophy (Tenets, layer architecture)
- `docs/USER_GUIDE_LLM.md` — compact API reference
- `docs/ARCHITECTURE.md` — layer structure and module boundaries
