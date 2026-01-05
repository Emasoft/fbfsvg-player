#pragma once

// Compile-time control: enabled in debug builds by default
#ifndef SVG_INSTRUMENTATION_ENABLED
    #if defined(DEBUG) || defined(_DEBUG) || !defined(NDEBUG)
        #define SVG_INSTRUMENTATION_ENABLED 1
    #else
        #define SVG_INSTRUMENTATION_ENABLED 0
    #endif
#endif

#if SVG_INSTRUMENTATION_ENABLED

#include <functional>
#include <string>

// Include the actual type definitions (forward declarations cause namespace issues)
#include "SVGTypes.h"

// Forward declaration for ThumbnailState from thumbnail_cache.h
namespace svgplayer {
    enum class ThumbnailState;
}

namespace svgplayer {
namespace instrumentation {

// Use the ThumbnailState from svgplayer namespace
using ::svgplayer::ThumbnailState;

// ============================================================================
// Hook Function Types
// ============================================================================

// ThumbnailCache hooks
using ThumbnailStateChangeHook = std::function<void(ThumbnailState state, const std::string& path)>;
using RequestQueuedHook = std::function<void(size_t queueSize)>;
using RequestDequeuedHook = std::function<void(size_t queueSize)>;
using LRUEvictionHook = std::function<void(int count)>;

// FolderBrowser hooks
using BrowserSVGRegeneratedHook = std::function<void()>;
using PageChangeHook = std::function<void(int page)>;
using SelectionChangeHook = std::function<void(int index)>;

// SVGAnimationController hooks
using FrameRenderedHook = std::function<void(const SVGRenderStats& stats)>;
using FrameSkippedHook = std::function<void(int frameIndex)>;
using AnimationLoopHook = std::function<void()>;
using AnimationEndHook = std::function<void()>;

// ============================================================================
// Thread-Safe Hook Setters (for runtime hook installation)
// ============================================================================

void setThumbnailStateChangeHook(ThumbnailStateChangeHook hook);
void setRequestQueuedHook(RequestQueuedHook hook);
void setRequestDequeuedHook(RequestDequeuedHook hook);
void setLRUEvictionHook(LRUEvictionHook hook);

void setBrowserSVGRegeneratedHook(BrowserSVGRegeneratedHook hook);
void setPageChangeHook(PageChangeHook hook);
void setSelectionChangeHook(SelectionChangeHook hook);

void setFrameRenderedHook(FrameRenderedHook hook);
void setFrameSkippedHook(FrameSkippedHook hook);
void setAnimationLoopHook(AnimationLoopHook hook);
void setAnimationEndHook(AnimationEndHook hook);

// ============================================================================
// Thread-Safe Hook Invocations (internal use by instrumented code)
// ============================================================================

void invokeThumbnailStateChange(ThumbnailState state, const std::string& path);
void invokeRequestQueued(size_t queueSize);
void invokeRequestDequeued(size_t queueSize);
void invokeLRUEviction(int count);

void invokeBrowserSVGRegenerated();
void invokePageChange(int page);
void invokeSelectionChange(int index);

void invokeFrameRendered(const SVGRenderStats& stats);
void invokeFrameSkipped(int frameIndex);
void invokeAnimationLoop();
void invokeAnimationEnd();

// ============================================================================
// RAII Hook Installer (for tests - automatically restores on scope exit)
// ============================================================================

class HookInstaller {
public:
    HookInstaller();
    ~HookInstaller();

    // Fluent interface for test setup
    HookInstaller& onThumbnailStateChange(ThumbnailStateChangeHook hook);
    HookInstaller& onRequestQueued(RequestQueuedHook hook);
    HookInstaller& onRequestDequeued(RequestDequeuedHook hook);
    HookInstaller& onLRUEviction(LRUEvictionHook hook);

    HookInstaller& onBrowserSVGRegenerated(BrowserSVGRegeneratedHook hook);
    HookInstaller& onPageChange(PageChangeHook hook);
    HookInstaller& onSelectionChange(SelectionChangeHook hook);

    HookInstaller& onFrameRendered(FrameRenderedHook hook);
    HookInstaller& onFrameSkipped(FrameSkippedHook hook);
    HookInstaller& onAnimationLoop(AnimationLoopHook hook);
    HookInstaller& onAnimationEnd(AnimationEndHook hook);

    // Non-copyable, non-movable (RAII resource)
    HookInstaller(const HookInstaller&) = delete;
    HookInstaller& operator=(const HookInstaller&) = delete;
    HookInstaller(HookInstaller&&) = delete;
    HookInstaller& operator=(HookInstaller&&) = delete;

private:
    // Bitfield to track which hooks were set (to restore only those)
    uint32_t installedMask{0};

    // Saved previous hooks (restored in destructor)
    ThumbnailStateChangeHook prevThumbnailStateChange;
    RequestQueuedHook prevRequestQueued;
    RequestDequeuedHook prevRequestDequeued;
    LRUEvictionHook prevLRUEviction;

    BrowserSVGRegeneratedHook prevBrowserSVGRegenerated;
    PageChangeHook prevPageChange;
    SelectionChangeHook prevSelectionChange;

    FrameRenderedHook prevFrameRendered;
    FrameSkippedHook prevFrameSkipped;
    AnimationLoopHook prevAnimationLoop;
    AnimationEndHook prevAnimationEnd;
};

} // namespace instrumentation
} // namespace svgplayer

// ============================================================================
// Convenience Macros (zero-overhead when SVG_INSTRUMENTATION_ENABLED=0)
// ============================================================================

#define SVG_INSTRUMENT_THUMBNAIL_STATE_CHANGE(state, path) \
    svgplayer::instrumentation::invokeThumbnailStateChange(state, path)
#define SVG_INSTRUMENT_REQUEST_QUEUED(queueSize) \
    svgplayer::instrumentation::invokeRequestQueued(queueSize)
#define SVG_INSTRUMENT_REQUEST_DEQUEUED(queueSize) \
    svgplayer::instrumentation::invokeRequestDequeued(queueSize)
#define SVG_INSTRUMENT_LRU_EVICTION(count) \
    svgplayer::instrumentation::invokeLRUEviction(count)

#define SVG_INSTRUMENT_BROWSER_SVG_REGENERATED() \
    svgplayer::instrumentation::invokeBrowserSVGRegenerated()
#define SVG_INSTRUMENT_PAGE_CHANGE(page) \
    svgplayer::instrumentation::invokePageChange(page)
#define SVG_INSTRUMENT_SELECTION_CHANGE(index) \
    svgplayer::instrumentation::invokeSelectionChange(index)

#define SVG_INSTRUMENT_FRAME_RENDERED(stats) \
    svgplayer::instrumentation::invokeFrameRendered(stats)
#define SVG_INSTRUMENT_FRAME_SKIPPED(frameIndex) \
    svgplayer::instrumentation::invokeFrameSkipped(frameIndex)
#define SVG_INSTRUMENT_ANIMATION_LOOP() \
    svgplayer::instrumentation::invokeAnimationLoop()
#define SVG_INSTRUMENT_ANIMATION_END() \
    svgplayer::instrumentation::invokeAnimationEnd()

// Generic hooks for extensibility (hook name as parameter)
// These provide flexibility for ad-hoc instrumentation points
// NOTE: These are placeholder no-ops. Use specific macros above for actual hooks.
#define SVG_INSTRUMENT_CALL(hookName) ((void)0)
#define SVG_INSTRUMENT_VALUE(hookName, ...) ((void)0)

#else // SVG_INSTRUMENTATION_ENABLED == 0

// ============================================================================
// Zero-Overhead Disabled Macros (compile out completely)
// ============================================================================

#define SVG_INSTRUMENT_THUMBNAIL_STATE_CHANGE(state, path) ((void)0)
#define SVG_INSTRUMENT_REQUEST_QUEUED(queueSize) ((void)0)
#define SVG_INSTRUMENT_REQUEST_DEQUEUED(queueSize) ((void)0)
#define SVG_INSTRUMENT_LRU_EVICTION(count) ((void)0)

#define SVG_INSTRUMENT_BROWSER_SVG_REGENERATED() ((void)0)
#define SVG_INSTRUMENT_PAGE_CHANGE(page) ((void)0)
#define SVG_INSTRUMENT_SELECTION_CHANGE(index) ((void)0)

#define SVG_INSTRUMENT_FRAME_RENDERED(stats) ((void)0)
#define SVG_INSTRUMENT_FRAME_SKIPPED(frameIndex) ((void)0)
#define SVG_INSTRUMENT_ANIMATION_LOOP() ((void)0)
#define SVG_INSTRUMENT_ANIMATION_END() ((void)0)

// Generic placeholder macros (no-ops when disabled)
#define SVG_INSTRUMENT_CALL(hookName) ((void)0)
#define SVG_INSTRUMENT_VALUE(hookName, ...) ((void)0)

#endif // SVG_INSTRUMENTATION_ENABLED
