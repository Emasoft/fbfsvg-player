// svg_instrumentation.cpp - Implementation of instrumentation hooks
// Copyright 2025 Emasoft. All rights reserved.
// SPDX-License-Identifier: MIT

#include "svg_instrumentation.h"

#if SVG_INSTRUMENTATION_ENABLED

#include <mutex>
#include <atomic>

namespace svgplayer {
namespace instrumentation {

// ============================================================================
// Thread-Safe Hook Storage
// ============================================================================

namespace {

// Mutex for hook access (read-write pattern)
std::mutex g_hookMutex;

// Hook storage
ThumbnailStateChangeHook g_thumbnailStateChangeHook;
RequestQueuedHook g_requestQueuedHook;
RequestDequeuedHook g_requestDequeuedHook;
LRUEvictionHook g_lruEvictionHook;

BrowserSVGRegeneratedHook g_browserSVGRegeneratedHook;
PageChangeHook g_pageChangeHook;
SelectionChangeHook g_selectionChangeHook;

FrameRenderedHook g_frameRenderedHook;
FrameSkippedHook g_frameSkippedHook;
AnimationLoopHook g_animationLoopHook;
AnimationEndHook g_animationEndHook;

// Helper to safely invoke a hook
template<typename HookType, typename... Args>
void safeInvoke(const HookType& hook, Args&&... args) {
    HookType localCopy;
    {
        std::lock_guard<std::mutex> lock(g_hookMutex);
        localCopy = hook;
    }
    if (localCopy) {
        localCopy(std::forward<Args>(args)...);
    }
}

// Helper to get hook copy for saving
template<typename HookType>
HookType getHookCopy(const HookType& hook) {
    std::lock_guard<std::mutex> lock(g_hookMutex);
    return hook;
}

// Helper to set hook and return previous
template<typename HookType>
HookType setHookAndGetPrevious(HookType& storage, HookType newHook) {
    std::lock_guard<std::mutex> lock(g_hookMutex);
    HookType prev = std::move(storage);
    storage = std::move(newHook);
    return prev;
}

} // anonymous namespace

// ============================================================================
// Thread-Safe Hook Setters
// ============================================================================

void setThumbnailStateChangeHook(ThumbnailStateChangeHook hook) {
    std::lock_guard<std::mutex> lock(g_hookMutex);
    g_thumbnailStateChangeHook = std::move(hook);
}

void setRequestQueuedHook(RequestQueuedHook hook) {
    std::lock_guard<std::mutex> lock(g_hookMutex);
    g_requestQueuedHook = std::move(hook);
}

void setRequestDequeuedHook(RequestDequeuedHook hook) {
    std::lock_guard<std::mutex> lock(g_hookMutex);
    g_requestDequeuedHook = std::move(hook);
}

void setLRUEvictionHook(LRUEvictionHook hook) {
    std::lock_guard<std::mutex> lock(g_hookMutex);
    g_lruEvictionHook = std::move(hook);
}

void setBrowserSVGRegeneratedHook(BrowserSVGRegeneratedHook hook) {
    std::lock_guard<std::mutex> lock(g_hookMutex);
    g_browserSVGRegeneratedHook = std::move(hook);
}

void setPageChangeHook(PageChangeHook hook) {
    std::lock_guard<std::mutex> lock(g_hookMutex);
    g_pageChangeHook = std::move(hook);
}

void setSelectionChangeHook(SelectionChangeHook hook) {
    std::lock_guard<std::mutex> lock(g_hookMutex);
    g_selectionChangeHook = std::move(hook);
}

void setFrameRenderedHook(FrameRenderedHook hook) {
    std::lock_guard<std::mutex> lock(g_hookMutex);
    g_frameRenderedHook = std::move(hook);
}

void setFrameSkippedHook(FrameSkippedHook hook) {
    std::lock_guard<std::mutex> lock(g_hookMutex);
    g_frameSkippedHook = std::move(hook);
}

void setAnimationLoopHook(AnimationLoopHook hook) {
    std::lock_guard<std::mutex> lock(g_hookMutex);
    g_animationLoopHook = std::move(hook);
}

void setAnimationEndHook(AnimationEndHook hook) {
    std::lock_guard<std::mutex> lock(g_hookMutex);
    g_animationEndHook = std::move(hook);
}

// ============================================================================
// Thread-Safe Hook Invocations
// ============================================================================

void invokeThumbnailStateChange(ThumbnailState state, const std::string& path) {
    safeInvoke(g_thumbnailStateChangeHook, state, path);
}

void invokeRequestQueued(size_t queueSize) {
    safeInvoke(g_requestQueuedHook, queueSize);
}

void invokeRequestDequeued(size_t queueSize) {
    safeInvoke(g_requestDequeuedHook, queueSize);
}

void invokeLRUEviction(int count) {
    safeInvoke(g_lruEvictionHook, count);
}

void invokeBrowserSVGRegenerated() {
    safeInvoke(g_browserSVGRegeneratedHook);
}

void invokePageChange(int page) {
    safeInvoke(g_pageChangeHook, page);
}

void invokeSelectionChange(int index) {
    safeInvoke(g_selectionChangeHook, index);
}

void invokeFrameRendered(const SVGRenderStats& stats) {
    safeInvoke(g_frameRenderedHook, stats);
}

void invokeFrameSkipped(int frameIndex) {
    safeInvoke(g_frameSkippedHook, frameIndex);
}

void invokeAnimationLoop() {
    safeInvoke(g_animationLoopHook);
}

void invokeAnimationEnd() {
    safeInvoke(g_animationEndHook);
}

// ============================================================================
// HookInstaller Implementation
// ============================================================================

// Bit masks for tracking installed hooks
enum HookMask : uint32_t {
    HOOK_THUMBNAIL_STATE_CHANGE = 1 << 0,
    HOOK_REQUEST_QUEUED         = 1 << 1,
    HOOK_REQUEST_DEQUEUED       = 1 << 2,
    HOOK_LRU_EVICTION           = 1 << 3,
    HOOK_BROWSER_SVG_REGENERATED = 1 << 4,
    HOOK_PAGE_CHANGE            = 1 << 5,
    HOOK_SELECTION_CHANGE       = 1 << 6,
    HOOK_FRAME_RENDERED         = 1 << 7,
    HOOK_FRAME_SKIPPED          = 1 << 8,
    HOOK_ANIMATION_LOOP         = 1 << 9,
    HOOK_ANIMATION_END          = 1 << 10,
};

HookInstaller::HookInstaller() : installedMask{0} {}

HookInstaller::~HookInstaller() {
    // Restore all previously saved hooks
    if (installedMask & HOOK_THUMBNAIL_STATE_CHANGE) {
        setThumbnailStateChangeHook(std::move(prevThumbnailStateChange));
    }
    if (installedMask & HOOK_REQUEST_QUEUED) {
        setRequestQueuedHook(std::move(prevRequestQueued));
    }
    if (installedMask & HOOK_REQUEST_DEQUEUED) {
        setRequestDequeuedHook(std::move(prevRequestDequeued));
    }
    if (installedMask & HOOK_LRU_EVICTION) {
        setLRUEvictionHook(std::move(prevLRUEviction));
    }
    if (installedMask & HOOK_BROWSER_SVG_REGENERATED) {
        setBrowserSVGRegeneratedHook(std::move(prevBrowserSVGRegenerated));
    }
    if (installedMask & HOOK_PAGE_CHANGE) {
        setPageChangeHook(std::move(prevPageChange));
    }
    if (installedMask & HOOK_SELECTION_CHANGE) {
        setSelectionChangeHook(std::move(prevSelectionChange));
    }
    if (installedMask & HOOK_FRAME_RENDERED) {
        setFrameRenderedHook(std::move(prevFrameRendered));
    }
    if (installedMask & HOOK_FRAME_SKIPPED) {
        setFrameSkippedHook(std::move(prevFrameSkipped));
    }
    if (installedMask & HOOK_ANIMATION_LOOP) {
        setAnimationLoopHook(std::move(prevAnimationLoop));
    }
    if (installedMask & HOOK_ANIMATION_END) {
        setAnimationEndHook(std::move(prevAnimationEnd));
    }
}

HookInstaller& HookInstaller::onThumbnailStateChange(ThumbnailStateChangeHook hook) {
    prevThumbnailStateChange = getHookCopy(g_thumbnailStateChangeHook);
    setThumbnailStateChangeHook(std::move(hook));
    installedMask |= HOOK_THUMBNAIL_STATE_CHANGE;
    return *this;
}

HookInstaller& HookInstaller::onRequestQueued(RequestQueuedHook hook) {
    prevRequestQueued = getHookCopy(g_requestQueuedHook);
    setRequestQueuedHook(std::move(hook));
    installedMask |= HOOK_REQUEST_QUEUED;
    return *this;
}

HookInstaller& HookInstaller::onRequestDequeued(RequestDequeuedHook hook) {
    prevRequestDequeued = getHookCopy(g_requestDequeuedHook);
    setRequestDequeuedHook(std::move(hook));
    installedMask |= HOOK_REQUEST_DEQUEUED;
    return *this;
}

HookInstaller& HookInstaller::onLRUEviction(LRUEvictionHook hook) {
    prevLRUEviction = getHookCopy(g_lruEvictionHook);
    setLRUEvictionHook(std::move(hook));
    installedMask |= HOOK_LRU_EVICTION;
    return *this;
}

HookInstaller& HookInstaller::onBrowserSVGRegenerated(BrowserSVGRegeneratedHook hook) {
    prevBrowserSVGRegenerated = getHookCopy(g_browserSVGRegeneratedHook);
    setBrowserSVGRegeneratedHook(std::move(hook));
    installedMask |= HOOK_BROWSER_SVG_REGENERATED;
    return *this;
}

HookInstaller& HookInstaller::onPageChange(PageChangeHook hook) {
    prevPageChange = getHookCopy(g_pageChangeHook);
    setPageChangeHook(std::move(hook));
    installedMask |= HOOK_PAGE_CHANGE;
    return *this;
}

HookInstaller& HookInstaller::onSelectionChange(SelectionChangeHook hook) {
    prevSelectionChange = getHookCopy(g_selectionChangeHook);
    setSelectionChangeHook(std::move(hook));
    installedMask |= HOOK_SELECTION_CHANGE;
    return *this;
}

HookInstaller& HookInstaller::onFrameRendered(FrameRenderedHook hook) {
    prevFrameRendered = getHookCopy(g_frameRenderedHook);
    setFrameRenderedHook(std::move(hook));
    installedMask |= HOOK_FRAME_RENDERED;
    return *this;
}

HookInstaller& HookInstaller::onFrameSkipped(FrameSkippedHook hook) {
    prevFrameSkipped = getHookCopy(g_frameSkippedHook);
    setFrameSkippedHook(std::move(hook));
    installedMask |= HOOK_FRAME_SKIPPED;
    return *this;
}

HookInstaller& HookInstaller::onAnimationLoop(AnimationLoopHook hook) {
    prevAnimationLoop = getHookCopy(g_animationLoopHook);
    setAnimationLoopHook(std::move(hook));
    installedMask |= HOOK_ANIMATION_LOOP;
    return *this;
}

HookInstaller& HookInstaller::onAnimationEnd(AnimationEndHook hook) {
    prevAnimationEnd = getHookCopy(g_animationEndHook);
    setAnimationEndHook(std::move(hook));
    installedMask |= HOOK_ANIMATION_END;
    return *this;
}

} // namespace instrumentation
} // namespace svgplayer

#endif // SVG_INSTRUMENTATION_ENABLED
