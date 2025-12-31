// SVGAnimationController.h - Shared animation controller for SVG playback
// Platform-independent C++17 implementation for SMIL animation parsing and playback control
// Used by: macOS player, Linux player, iOS SDK, Linux SDK
//
// This class handles all animation logic without any Skia dependencies.
// Platform code is responsible for:
// - Skia DOM manipulation (findNodeById, setAttribute)
// - Pixel buffer rendering
// - Display/window management
// - Threading

#ifndef SVG_ANIMATION_CONTROLLER_H
#define SVG_ANIMATION_CONTROLLER_H

#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <cmath>
#include <functional>

namespace svgplayer {

// Playback state enum - represents the current state of the animation timeline
enum class PlaybackState {
    Stopped,    // Timeline at 0, not advancing
    Playing,    // Timeline advancing forward (or reverse if rate < 0)
    Paused      // Timeline frozen at current position
};

// Repeat mode enum - determines behavior when animation reaches end
enum class RepeatMode {
    None,       // Play once and stop at end
    Loop,       // Loop back to start when reaching end
    Reverse,    // Reverse direction when reaching end (ping-pong)
    Count       // Repeat specific number of times then stop
};

// SMIL Animation data structure - one per <animate> element in the SVG
// Extracted from macOS player lines 132-179
struct SMILAnimation {
    std::string targetId;           // ID of element to animate (e.g., "frame1")
    std::string attributeName;      // Attribute to animate (e.g., "xlink:href")
    std::vector<std::string> values; // Discrete values to cycle through (frame refs)
    double duration;                 // Total animation duration in seconds
    bool repeat;                     // Whether to repeat indefinitely
    std::string calcMode;           // Interpolation mode: "discrete", "linear", etc.

    // Get current value based on elapsed time
    // For discrete mode, divides duration equally among all values
    std::string getCurrentValue(double elapsedSeconds) const;

    // Get current frame index (0-based) based on elapsed time
    // Useful for frame-based animations with discrete values
    size_t getCurrentFrameIndex(double elapsedSeconds) const;

    // Get total number of frames/values
    size_t getFrameCount() const { return values.size(); }
};

// Animation state for current frame - returned to renderers
// Contains the resolved attribute value for a specific target element
struct AnimationState {
    std::string targetId;           // Element ID to update
    std::string attributeName;      // Attribute to modify
    std::string value;              // Current value to set
};

// Render statistics - performance metrics for the animation
struct AnimationStats {
    double renderTimeMs;            // Time spent in Skia rendering
    double updateTimeMs;            // Time spent updating animation state
    double animationTimeMs;         // Current animation elapsed time
    int currentFrame;               // Current frame index (0-based)
    int totalFrames;                // Total frames in animation
    double fps;                     // Calculated frames per second
    size_t frameSkips;              // Number of skipped frames (if any)
};

// SVGAnimationController - Main class for SVG animation playback
// Thread-safety: This class is NOT thread-safe. All method calls must be
// serialized by the caller. Typically, all methods should be called from
// the main/UI thread, or protected by an external mutex.
class SVGAnimationController {
public:
    // === Initialization ===
    SVGAnimationController();
    ~SVGAnimationController();

    // Disable copy (contains non-copyable state)
    SVGAnimationController(const SVGAnimationController&) = delete;
    SVGAnimationController& operator=(const SVGAnimationController&) = delete;

    // Disable move (contains std::mutex which cannot be moved)
    SVGAnimationController(SVGAnimationController&&) = delete;
    SVGAnimationController& operator=(SVGAnimationController&&) = delete;

    // === SVG Content & Parsing ===

    // Load SVG from string content - parses animations and prepares playback
    // Returns true if parsing succeeded and animations were found
    bool loadFromContent(const std::string& svgContent);

    // Load SVG from file path - reads file and calls loadFromContent
    bool loadFromFile(const std::string& filepath);

    // Unload current SVG and reset state
    void unload();

    // Check if SVG is loaded and has animations
    bool isLoaded() const;

    // Get preprocessed SVG content (with synthetic IDs injected for <use> elements)
    // Renderers should use this content instead of the original file
    const std::string& getProcessedContent() const;

    // Get original (unmodified) SVG content
    const std::string& getOriginalContent() const;

    // Preprocess SVG content without loading it (for DOM parsing before animation loading)
    // Returns the preprocessed content with synthetic IDs injected
    std::string getPreprocessedContent(const std::string& svgContent);

    // === Animation Info ===

    // Get total animation duration in seconds
    double getDuration() const;

    // Get total number of frames (based on animation with most values)
    int getTotalFrames() const;

    // Get animation frame rate (frames per second)
    float getFrameRate() const;

    // Get all parsed SMIL animations
    const std::vector<SMILAnimation>& getAnimations() const;

    // Check if SVG has any animations
    bool hasAnimations() const;

    // === Playback Control ===

    // Start playback from current position
    void play();

    // Pause playback at current position
    void pause();

    // Stop playback and reset to start
    void stop();

    // Toggle between play and pause
    void togglePlayback();

    // Get current playback state
    PlaybackState getPlaybackState() const;

    // Check convenience methods
    bool isPlaying() const { return playbackState_ == PlaybackState::Playing; }
    bool isPaused() const { return playbackState_ == PlaybackState::Paused; }
    bool isStopped() const { return playbackState_ == PlaybackState::Stopped; }

    // === Repeat Mode ===

    // Set repeat mode (None, Loop, Reverse, Count)
    void setRepeatMode(RepeatMode mode);
    RepeatMode getRepeatMode() const;

    // Set number of times to repeat (only used when mode is Count)
    void setRepeatCount(int count);
    int getRepeatCount() const;

    // Get number of completed loops
    int getCompletedLoops() const;

    // Check if currently playing forward (false when in reverse phase of ping-pong)
    bool isPlayingForward() const;

    // === Playback Rate ===

    // Set playback rate multiplier (0.1 to 10.0, default 1.0)
    // Negative values play in reverse
    void setPlaybackRate(float rate);
    float getPlaybackRate() const;

    // === Timeline ===

    // Update the animation timeline by deltaTime seconds
    // Call this from the render loop with the time since last frame
    // Returns true if animation state changed (needs re-render)
    bool update(double deltaTime);

    // Get current time position in seconds
    double getCurrentTime() const;

    // Get current progress as a fraction (0.0 to 1.0)
    float getProgress() const;

    // Get current frame index (0-based)
    int getCurrentFrame() const;

    // === Seeking ===

    // Seek to absolute time in seconds
    void seekTo(double timeSeconds);

    // Seek to specific frame index (0-based)
    void seekToFrame(int frame);

    // Seek to progress position (0.0 to 1.0)
    void seekToProgress(float progress);

    // Seek to start (time = 0)
    void seekToStart();

    // Seek to end (time = duration)
    void seekToEnd();

    // === Frame Stepping ===

    // Step forward by one frame (pauses playback)
    void stepForward();

    // Step backward by one frame (pauses playback)
    void stepBackward();

    // Step by specified number of frames (negative for backward)
    void stepByFrames(int frames);

    // === Relative Seeking ===

    // Seek forward by specified seconds
    void seekForwardByTime(double seconds);

    // Seek backward by specified seconds
    void seekBackwardByTime(double seconds);

    // Seek forward by percentage of duration (e.g., 0.1 = 10%)
    void seekForwardByPercentage(float percent);

    // Seek backward by percentage of duration
    void seekBackwardByPercentage(float percent);

    // === Scrubbing ===

    // Begin scrubbing mode - saves current state and pauses
    void beginScrubbing();

    // Scrub to position (progress 0.0 to 1.0) - only works when scrubbing
    void scrubToProgress(float progress);

    // End scrubbing mode - optionally resume previous playback state
    void endScrubbing(bool resume);

    // Check if currently in scrubbing mode
    bool isScrubbing() const;

    // === Animation State Query ===

    // Get current animation states for all animated elements
    // Called by renderers to update Skia DOM attributes
    std::vector<AnimationState> getCurrentAnimationStates() const;

    // === Statistics ===

    // Get current animation statistics
    AnimationStats getStats() const;

    // Reset statistics counters
    void resetStats();

    // Update render time stat (called by renderer after Skia render)
    void updateRenderTime(double timeMs);

    // === Utility ===

    // Format time as MM:SS.mmm string
    static std::string formatTime(double seconds);

    // Convert time to frame index
    int frameForTime(double time) const;

    // Convert frame index to time
    double timeForFrame(int frame) const;

    // === Event Callbacks (optional) ===

    // Callback when playback state changes
    using StateChangeCallback = std::function<void(PlaybackState newState)>;
    void setStateChangeCallback(StateChangeCallback callback);

    // Callback when animation loops
    using LoopCallback = std::function<void(int loopCount)>;
    void setLoopCallback(LoopCallback callback);

    // Callback when animation reaches end (non-looping)
    using EndCallback = std::function<void()>;
    void setEndCallback(EndCallback callback);

private:
    // === Private Parsing Methods ===

    // Parse SMIL animations from content string
    std::vector<SMILAnimation> parseAnimations(const std::string& content);

    // Preprocess SVG to inject synthetic IDs and convert <symbol> to <g>
    std::string preprocessSVG(const std::string& content);

    // Convert <symbol> elements to <g> (Skia doesn't support <symbol>)
    static std::string convertSymbolsToGroups(const std::string& content);

    // Parse duration string (e.g., "1.5s", "500ms") to seconds
    static double parseDuration(const std::string& durStr);

    // Extract attribute value from tag string
    static std::string extractAttribute(const std::string& tag, const std::string& attrName);

    // Find last occurrence of pattern before endPos
    static size_t findLastOf(const std::string& str, const std::string& pattern, size_t endPos);

    // === Private State ===

    // Loaded content
    std::vector<SMILAnimation> animations_;
    std::string processedContent_;
    std::string originalContent_;
    std::map<size_t, std::string> syntheticIds_;
    bool loaded_;

    // Timeline state (using steady_clock for SMIL-compliant monotonic time)
    double currentTime_;
    double duration_;
    float frameRate_;
    int totalFrames_;

    // Playback state
    PlaybackState playbackState_;
    RepeatMode repeatMode_;
    int repeatCount_;
    int completedLoops_;
    float playbackRate_;
    bool playingForward_;

    // Scrubbing state
    bool scrubbing_;
    PlaybackState stateBeforeScrub_;

    // Statistics
    AnimationStats stats_;
    size_t lastFrameIndex_;
    std::chrono::steady_clock::time_point lastUpdateTime_;

    // Callbacks
    StateChangeCallback stateChangeCallback_;
    LoopCallback loopCallback_;
    EndCallback endCallback_;

    // Thread safety
    mutable std::mutex mutex_;

    // Internal helpers
    void clampCurrentTime();
    void handleLoopBehavior();
    void notifyStateChange(PlaybackState newState);
};

} // namespace svgplayer

#endif // SVG_ANIMATION_CONTROLLER_H
