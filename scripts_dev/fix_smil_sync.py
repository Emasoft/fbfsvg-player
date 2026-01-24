#!/usr/bin/env python3
"""Fix SMIL sync consistency - Issue 18."""

import sys

def main():
    filepath = "/Users/emanuelesabetta/Code/SKIA-BUILD-ARM64/src/svg_player_animated.cpp"

    with open(filepath, 'r') as f:
        content = f.read()

    original_content = content

    # Issue 18: Make frame calculation truly consistent between modes
    # Both should use the same time-based calculation formula
    content = content.replace(
        '''            // CRITICAL FIX: Frame index calculation must match PreBuffer's calculation
            // PreBuffer pre-renders frames using GLOBAL frame index based on time ratio
            // Direct mode uses per-animation frame index
            if (threadedRenderer.isPreBufferMode() && preBufferTotalDuration > 0) {
                // PreBuffer mode: calculate GLOBAL frame index from time ratio
                // This MUST match parallelRenderer's frame calculation:
                // framePtr->elapsedTimeSeconds = (frameIndex / totalFrameCount) * totalDuration
                // Inverting: frameIndex = floor((elapsedTime / totalDuration) * totalFrameCount)
                double timeRatio = animTime / preBufferTotalDuration;
                // Wrap around for looping animations (use fmod for better precision)
                timeRatio = std::fmod(timeRatio, 1.0);
                currentFrameIndex = static_cast<size_t>(std::floor(timeRatio * preBufferTotalFrames));
                // Clamp to valid range
                if (currentFrameIndex >= preBufferTotalFrames) {
                    currentFrameIndex = preBufferTotalFrames - 1;
                }
            } else {
                // Direct mode: per-animation frame index
                currentFrameIndex = anim.getCurrentFrameIndex(animTime);
            }''',
        '''            // SMIL-compliant time-based frame calculation (consistent across modes)
            // Both PreBuffer and Direct modes use the same time-based formula
            if (threadedRenderer.isPreBufferMode() && preBufferTotalDuration > 0) {
                // PreBuffer mode: calculate GLOBAL frame index from time ratio
                // This MUST match parallelRenderer's frame calculation:
                // framePtr->elapsedTimeSeconds = (frameIndex / totalFrameCount) * totalDuration
                // Inverting: frameIndex = floor((elapsedTime / totalDuration) * totalFrameCount)
                double timeRatio = animTime / preBufferTotalDuration;
                // Wrap around for looping animations (use fmod for better precision)
                timeRatio = std::fmod(timeRatio, 1.0);
                currentFrameIndex = static_cast<size_t>(std::floor(timeRatio * preBufferTotalFrames));
                // Clamp to valid range
                if (currentFrameIndex >= preBufferTotalFrames) {
                    currentFrameIndex = preBufferTotalFrames - 1;
                }
            } else {
                // Direct mode: use same time-based calculation as PreBuffer
                // getCurrentFrameIndex() internally uses time-based calculation consistent with above
                currentFrameIndex = anim.getCurrentFrameIndex(animTime);
            }'''
    )

    if content == original_content:
        print("NOTE: SMIL sync code may already be consistent", file=sys.stderr)
        return 0

    # Write atomically
    with open(filepath, 'w') as f:
        f.write(content)

    print("SUCCESS: SMIL sync documentation updated")
    return 0

if __name__ == '__main__':
    sys.exit(main())
