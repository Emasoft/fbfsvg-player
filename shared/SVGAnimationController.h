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
#include <mutex>
#include <atomic>
#include <cstdint>

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

/**
 * @brief Main class for SVG animation playback
 *
 * @section thread_safety Thread Safety
 *
 * This class is NOT thread-safe. All method calls must be serialized by the caller.
 *
 * @par Thread Model
 * - All public methods should be called from the same thread (typically main/UI thread)
 * - The internal mutex_ provides protection only for internal state consistency
 * - External synchronization is required if calling from multiple threads
 *
 * @par Callback Execution Context
 * - All callbacks (StateChangeCallback, LoopCallback, EndCallback) are invoked synchronously
 * - Callbacks execute on the same thread that called the triggering method
 * - Methods that can trigger callbacks: play(), pause(), stop(), update(), seekTo*()
 * - Callbacks MUST NOT call back into SVGAnimationController methods (will deadlock)
 * - To call methods from callbacks, post to main thread queue or use delayed invocation
 *
 * @par Safe Callback Operations
 * - Query UI state, update UI elements, schedule future operations
 * - Post tasks to main thread/run loop for SVGAnimationController method calls
 *
 * @par Unsafe Callback Operations
 * - Direct calls to play(), pause(), stop(), seekTo*() (deadlock risk)
 * - Modifying shared state without synchronization
 *
 * @par Mutex Protection (mutex_)
 * The internal mutex_ protects:
 * - Internal state reads/writes during update()
 * - Statistics updates
 * - Animation state queries
 *
 * The mutex_ does NOT protect:
 * - Sequences of method calls (caller must serialize)
 * - Callback invocations (callbacks run with mutex held - do not re-enter)
 */
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

    /**
     * @section svg_content SVG Content & Parsing
     * @threadsafe No - must be called from main thread only
     * @note Call stop() before loading new content to avoid race conditions
     */

    /**
     * @brief Load SVG from string content - parses animations and prepares playback
     * @param svgContent The SVG content string to parse
     * @return true if parsing succeeded and animations were found
     * @threadsafe No - must be called from main thread only
     * @note Resets playback state to Stopped
     */
    bool loadFromContent(const std::string& svgContent);

    /**
     * @brief Load SVG from file path - reads file and calls loadFromContent
     * @param filepath Path to SVG file
     * @return true if file was read and parsed successfully
     * @threadsafe No - must be called from main thread only
     */
    bool loadFromFile(const std::string& filepath);

    /**
     * @brief Unload current SVG and reset state
     * @threadsafe No - must be called from main thread only
     * @note Stops playback and clears all animation data
     */
    void unload();

    /**
     * @brief Check if SVG is loaded and has animations
     * @return true if content is loaded
     * @threadsafe Yes - read-only query, protected by mutex_
     */
    bool isLoaded() const;

    /**
     * @brief Get preprocessed SVG content (with synthetic IDs injected for <use> elements)
     * @return Reference to processed content string
     * @threadsafe Yes - read-only query, protected by mutex_
     * @note Renderers should use this content instead of the original file
     */
    const std::string& getProcessedContent() const;

    /**
     * @brief Get original (unmodified) SVG content
     * @return Reference to original content string
     * @threadsafe Yes - read-only query, protected by mutex_
     */
    const std::string& getOriginalContent() const;

    /**
     * @brief Preprocess SVG content without loading it (for DOM parsing before animation loading)
     * @param svgContent The SVG content to preprocess
     * @return The preprocessed content with synthetic IDs injected
     * @threadsafe Yes - stateless utility function
     */
    std::string getPreprocessedContent(const std::string& svgContent);

    /**
     * @section animation_info Animation Info
     * @threadsafe Yes - read-only queries, protected by mutex_
     */

    /**
     * @brief Get total animation duration in seconds
     * @return Animation duration in seconds
     * @threadsafe Yes - read-only query, protected by mutex_
     */
    double getDuration() const;

    /**
     * @brief Get total number of frames (based on animation with most values)
     * @return Total frame count
     * @threadsafe Yes - read-only query, protected by mutex_
     */
    int getTotalFrames() const;

    /**
     * @brief Get animation frame rate (frames per second)
     * @return Frame rate in fps
     * @threadsafe Yes - read-only query, protected by mutex_
     */
    float getFrameRate() const;

    /**
     * @brief Get all parsed SMIL animations
     * @return Reference to animations vector
     * @threadsafe Yes - read-only query, protected by mutex_
     */
    const std::vector<SMILAnimation>& getAnimations() const;

    /**
     * @brief Check if SVG has any animations
     * @return true if animations exist
     * @threadsafe Yes - read-only query, protected by mutex_
     */
    bool hasAnimations() const;

    /**
     * @section playback_control Playback Control
     * @threadsafe No - must be called from main thread only
     * @warning These methods may invoke StateChangeCallback synchronously
     * @note Do NOT call these methods from within callbacks (deadlock risk)
     */

    /**
     * @brief Start playback from current position
     * @threadsafe No - must be called from main thread only
     * @note May invoke StateChangeCallback(Playing) synchronously if state changes
     * @warning Do NOT call from within any callback
     */
    void play();

    /**
     * @brief Pause playback at current position
     * @threadsafe No - must be called from main thread only
     * @note May invoke StateChangeCallback(Paused) synchronously if state changes
     * @warning Do NOT call from within any callback
     */
    void pause();

    /**
     * @brief Stop playback and reset to start
     * @threadsafe No - must be called from main thread only
     * @note May invoke StateChangeCallback(Stopped) synchronously if state changes
     * @warning Do NOT call from within any callback
     */
    void stop();

    /**
     * @brief Toggle between play and pause
     * @threadsafe No - must be called from main thread only
     * @note May invoke StateChangeCallback synchronously
     * @warning Do NOT call from within any callback
     */
    void togglePlayback();

    /**
     * @brief Get current playback state
     * @return Current PlaybackState value
     * @threadsafe Yes - read-only query, protected by mutex_
     */
    PlaybackState getPlaybackState() const;

    /**
     * @brief Check if currently playing
     * @return true if state is Playing
     * @threadsafe Yes - read-only query
     */
    bool isPlaying() const { return playbackState_ == PlaybackState::Playing; }

    /**
     * @brief Check if currently paused
     * @return true if state is Paused
     * @threadsafe Yes - read-only query
     */
    bool isPaused() const { return playbackState_ == PlaybackState::Paused; }

    /**
     * @brief Check if currently stopped
     * @return true if state is Stopped
     * @threadsafe Yes - read-only query
     */
    bool isStopped() const { return playbackState_ == PlaybackState::Stopped; }

    /**
     * @section repeat_mode Repeat Mode
     * @threadsafe No for setters, Yes for getters
     */

    /**
     * @brief Set repeat mode (None, Loop, Reverse, Count)
     * @param mode The desired RepeatMode
     * @threadsafe No - must be called from main thread only
     */
    void setRepeatMode(RepeatMode mode);

    /**
     * @brief Get current repeat mode
     * @return Current RepeatMode value
     * @threadsafe Yes - read-only query, protected by mutex_
     */
    RepeatMode getRepeatMode() const;

    /**
     * @brief Set number of times to repeat (only used when mode is Count)
     * @param count Number of loops (must be > 0)
     * @threadsafe No - must be called from main thread only
     */
    void setRepeatCount(int count);

    /**
     * @brief Get configured repeat count
     * @return Repeat count value
     * @threadsafe Yes - read-only query, protected by mutex_
     */
    int getRepeatCount() const;

    /**
     * @brief Get number of completed loops
     * @return Number of times animation has looped
     * @threadsafe Yes - read-only query, protected by mutex_
     */
    int getCompletedLoops() const;

    /**
     * @brief Check if currently playing forward (false when in reverse phase of ping-pong)
     * @return true if playing forward, false if playing reverse
     * @threadsafe Yes - read-only query, protected by mutex_
     */
    bool isPlayingForward() const;

    /**
     * @section playback_rate Playback Rate
     * @threadsafe No for setter, Yes for getter
     */

    /**
     * @brief Set playback rate multiplier (0.1 to 10.0, default 1.0)
     * @param rate Speed multiplier (negative values play in reverse)
     * @threadsafe No - must be called from main thread only
     * @note Values outside [0.1, 10.0] will be clamped
     */
    void setPlaybackRate(float rate);

    /**
     * @brief Get current playback rate
     * @return Playback rate multiplier
     * @threadsafe Yes - read-only query, protected by mutex_
     */
    float getPlaybackRate() const;

    /**
     * @section timeline Timeline Control
     * @threadsafe update() has partial thread safety - see method documentation
     * @threadsafe Getters are thread-safe
     */

    /**
     * @brief Update the animation timeline by deltaTime seconds
     * @param deltaTime Time elapsed since last update in seconds
     * @return true if animation state changed (needs re-render)
     *
     * @threadsafe Partial - internal state protected by mutex_
     * @warning MUST be called from render loop thread (typically main thread)
     * @note This is the ONLY method that should be called from render loop
     * @note May invoke callbacks synchronously:
     *       - LoopCallback when animation loops
     *       - EndCallback when animation ends (non-looping)
     *       - StateChangeCallback if playback stops due to end
     * @warning Callbacks execute with mutex_ held - do NOT call back into controller
     *
     * @par Callback Safety in update()
     * - LoopCallback: Triggered when currentTime wraps around
     * - EndCallback: Triggered when animation reaches end and stops
     * - StateChangeCallback: Triggered if playback auto-stops at end
     * - All callbacks run synchronously before update() returns
     * - To call controller methods from callbacks, post to main thread queue
     *
     * @par Typical Usage
     * @code
     * // In render loop (main thread):
     * double deltaTime = calculateDeltaTime();
     * if (controller.update(deltaTime)) {
     *     // Animation state changed - re-render SVG
     *     render();
     * }
     * @endcode
     */
    bool update(double deltaTime);

    /**
     * @brief Get current time position in seconds
     * @return Current timeline position
     * @threadsafe Yes - read-only query, protected by mutex_
     */
    double getCurrentTime() const;

    /**
     * @brief Get current progress as a fraction (0.0 to 1.0)
     * @return Progress value (0.0 = start, 1.0 = end)
     * @threadsafe Yes - read-only query, protected by mutex_
     */
    float getProgress() const;

    /**
     * @brief Get current frame index (0-based)
     * @return Current frame number
     * @threadsafe Yes - read-only query, protected by mutex_
     */
    int getCurrentFrame() const;

    /**
     * @section seeking Seeking
     * @threadsafe No - must be called from main thread only
     * @warning These methods may invoke callbacks if seek crosses loop boundary
     */

    /**
     * @brief Seek to absolute time in seconds
     * @param timeSeconds Target time position (clamped to [0, duration])
     * @threadsafe No - must be called from main thread only
     * @note May invoke LoopCallback if seek crosses loop boundary
     * @warning Do NOT call from within any callback
     */
    void seekTo(double timeSeconds);

    /**
     * @brief Seek to specific frame index (0-based)
     * @param frame Target frame number (clamped to valid range)
     * @threadsafe No - must be called from main thread only
     * @note May invoke LoopCallback if seek crosses loop boundary
     * @warning Do NOT call from within any callback
     */
    void seekToFrame(int frame);

    /**
     * @brief Seek to progress position (0.0 to 1.0)
     * @param progress Target progress (0.0 = start, 1.0 = end)
     * @threadsafe No - must be called from main thread only
     * @note May invoke LoopCallback if seek crosses loop boundary
     * @warning Do NOT call from within any callback
     */
    void seekToProgress(float progress);

    /**
     * @brief Seek to start (time = 0)
     * @threadsafe No - must be called from main thread only
     * @note May invoke LoopCallback if currently at non-zero time
     * @warning Do NOT call from within any callback
     */
    void seekToStart();

    /**
     * @brief Seek to end (time = duration)
     * @threadsafe No - must be called from main thread only
     * @note May invoke LoopCallback or EndCallback
     * @warning Do NOT call from within any callback
     */
    void seekToEnd();

    /**
     * @section frame_stepping Frame Stepping
     * @threadsafe No - must be called from main thread only
     * @note These methods pause playback and may invoke StateChangeCallback
     */

    /**
     * @brief Step forward by one frame (pauses playback)
     * @threadsafe No - must be called from main thread only
     * @note Automatically pauses playback if currently playing
     * @note May invoke StateChangeCallback(Paused)
     * @warning Do NOT call from within any callback
     */
    void stepForward();

    /**
     * @brief Step backward by one frame (pauses playback)
     * @threadsafe No - must be called from main thread only
     * @note Automatically pauses playback if currently playing
     * @note May invoke StateChangeCallback(Paused)
     * @warning Do NOT call from within any callback
     */
    void stepBackward();

    /**
     * @brief Step by specified number of frames (negative for backward)
     * @param frames Number of frames to step (negative for backward)
     * @threadsafe No - must be called from main thread only
     * @note Automatically pauses playback if currently playing
     * @note May invoke StateChangeCallback(Paused)
     * @warning Do NOT call from within any callback
     */
    void stepByFrames(int frames);

    /**
     * @section relative_seeking Relative Seeking
     * @threadsafe No - must be called from main thread only
     * @note These methods may invoke callbacks if seek crosses boundaries
     */

    /**
     * @brief Seek forward by specified seconds
     * @param seconds Time to advance (positive value)
     * @threadsafe No - must be called from main thread only
     * @note May invoke LoopCallback if seek crosses loop boundary
     * @warning Do NOT call from within any callback
     */
    void seekForwardByTime(double seconds);

    /**
     * @brief Seek backward by specified seconds
     * @param seconds Time to rewind (positive value)
     * @threadsafe No - must be called from main thread only
     * @note May invoke LoopCallback if seek crosses loop boundary
     * @warning Do NOT call from within any callback
     */
    void seekBackwardByTime(double seconds);

    /**
     * @brief Seek forward by percentage of duration (e.g., 0.1 = 10%)
     * @param percent Percentage to advance (0.0 to 1.0)
     * @threadsafe No - must be called from main thread only
     * @note May invoke LoopCallback if seek crosses loop boundary
     * @warning Do NOT call from within any callback
     */
    void seekForwardByPercentage(float percent);

    /**
     * @brief Seek backward by percentage of duration
     * @param percent Percentage to rewind (0.0 to 1.0)
     * @threadsafe No - must be called from main thread only
     * @note May invoke LoopCallback if seek crosses loop boundary
     * @warning Do NOT call from within any callback
     */
    void seekBackwardByPercentage(float percent);

    /**
     * @section scrubbing Scrubbing
     * @threadsafe No - must be called from main thread only
     * @note Scrubbing mode is for interactive timeline scrubbing (e.g., dragging slider)
     */

    /**
     * @brief Begin scrubbing mode - saves current state and pauses
     * @threadsafe No - must be called from main thread only
     * @note Pauses playback and saves state for later resumption
     * @note May invoke StateChangeCallback(Paused)
     * @warning Do NOT call from within any callback
     */
    void beginScrubbing();

    /**
     * @brief Scrub to position (progress 0.0 to 1.0) - only works when scrubbing
     * @param progress Target progress position (0.0 to 1.0)
     * @threadsafe No - must be called from main thread only
     * @note No-op if not in scrubbing mode
     * @note Does not invoke callbacks (scrubbing is silent)
     */
    void scrubToProgress(float progress);

    /**
     * @brief End scrubbing mode - optionally resume previous playback state
     * @param resume If true, restore playback state from before scrubbing
     * @threadsafe No - must be called from main thread only
     * @note May invoke StateChangeCallback if resuming playback
     * @warning Do NOT call from within any callback
     */
    void endScrubbing(bool resume);

    /**
     * @brief Check if currently in scrubbing mode
     * @return true if scrubbing is active
     * @threadsafe Yes - read-only query, protected by mutex_
     */
    bool isScrubbing() const;

    /**
     * @section animation_state Animation State Query
     * @threadsafe Yes - read-only query, protected by mutex_
     */

    /**
     * @brief Get current animation states for all animated elements
     * @return Vector of AnimationState structures for current time
     * @threadsafe Yes - read-only query, protected by mutex_
     * @note Called by renderers to update Skia DOM attributes
     * @note Safe to call from render loop
     */
    std::vector<AnimationState> getCurrentAnimationStates() const;

    /**
     * @section statistics Statistics
     * @threadsafe Yes for getter, Partial for updaters
     */

    /**
     * @brief Get current animation statistics
     * @return AnimationStats structure with performance metrics
     * @threadsafe Yes - read-only query, protected by mutex_
     */
    AnimationStats getStats() const;

    /**
     * @brief Reset statistics counters
     * @threadsafe No - must be called from main thread only
     */
    void resetStats();

    /**
     * @brief Update render time stat (called by renderer after Skia render)
     * @param timeMs Render time in milliseconds
     * @threadsafe Partial - protected by mutex_, can be called from render thread
     * @note This is safe to call from render loop
     */
    void updateRenderTime(double timeMs);

    /**
     * @section utility Utility Functions
     * @threadsafe Yes - all utility functions are thread-safe
     */

    /**
     * @brief Format time as MM:SS.mmm string
     * @param seconds Time value in seconds
     * @return Formatted time string
     * @threadsafe Yes - static utility function
     */
    static std::string formatTime(double seconds);

    /**
     * @brief Convert time to frame index
     * @param time Time position in seconds
     * @return Frame index (0-based)
     * @threadsafe Yes - read-only calculation, protected by mutex_
     */
    int frameForTime(double time) const;

    /**
     * @brief Convert frame index to time
     * @param frame Frame index (0-based)
     * @return Time position in seconds
     * @threadsafe Yes - read-only calculation, protected by mutex_
     */
    double timeForFrame(int frame) const;

    /**
     * @section event_callbacks Event Callbacks (Optional)
     * @threadsafe No for setters, special considerations for callback execution
     *
     * @par Callback Execution Model
     * All callbacks are invoked SYNCHRONOUSLY on the same thread that triggered them.
     * Callbacks execute with the internal mutex_ held to prevent race conditions.
     *
     * @par Critical Callback Restrictions
     * - NEVER call SVGAnimationController methods directly from callbacks (deadlock)
     * - NEVER block or perform long operations in callbacks
     * - DO post tasks to main thread/run loop for controller method calls
     * - DO update UI state or schedule future operations
     *
     * @par When Callbacks Fire
     * - StateChangeCallback: play(), pause(), stop(), update() (on auto-stop)
     * - LoopCallback: update() (when currentTime wraps), seekTo*() (crossing boundary)
     * - EndCallback: update() (reaching end in non-loop mode), seekToEnd()
     *
     * @par Safe Callback Pattern (macOS example)
     * @code
     * controller.setStateChangeCallback([weak_self](PlaybackState state) {
     *     dispatch_async(dispatch_get_main_queue(), ^{
     *         // Now safe to call controller methods
     *         [weak_self handleStateChange:state];
     *     });
     * });
     * @endcode
     *
     * @par Unsafe Callback Pattern (DEADLOCK)
     * @code
     * controller.setStateChangeCallback([&](PlaybackState state) {
     *     controller.pause();  // DEADLOCK - mutex already held!
     * });
     * @endcode
     */

    /**
     * @brief Set callback for playback state changes
     * @param callback Function called when playback state changes (Playing/Paused/Stopped)
     * @threadsafe No - must be called from main thread only
     * @warning Callback executes synchronously with mutex_ held - do NOT call back into controller
     * @note Pass nullptr to clear the callback
     */
    using StateChangeCallback = std::function<void(PlaybackState newState)>;
    void setStateChangeCallback(StateChangeCallback callback);

    /**
     * @brief Set callback for animation loops
     * @param callback Function called when animation completes a loop (receives loop count)
     * @threadsafe No - must be called from main thread only
     * @warning Callback executes synchronously with mutex_ held - do NOT call back into controller
     * @note Pass nullptr to clear the callback
     */
    using LoopCallback = std::function<void(int loopCount)>;
    void setLoopCallback(LoopCallback callback);

    /**
     * @brief Set callback for animation end (non-looping mode)
     * @param callback Function called when animation reaches end and stops
     * @threadsafe No - must be called from main thread only
     * @warning Callback executes synchronously with mutex_ held - do NOT call back into controller
     * @note Pass nullptr to clear the callback
     */
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

    /**
     * @brief Internal mutex for protecting shared state
     *
     * This mutex provides thread safety for:
     * - Read operations (const methods returning internal state)
     * - update() method (protects timeline advancement)
     * - Statistics updates (updateRenderTime)
     * - Animation state queries (getCurrentAnimationStates)
     *
     * The mutex does NOT protect:
     * - Sequences of method calls (caller must serialize)
     * - Callback invocations (callbacks execute with mutex held)
     *
     * @warning Callbacks execute with this mutex held
     *          DO NOT call controller methods from callbacks (deadlock)
     *
     * @note Marked mutable to allow locking in const methods
     */
    mutable std::mutex mutex_;

    // Internal helpers
    void clampCurrentTime();
    void handleLoopBehavior();
    void notifyStateChange(PlaybackState newState);
};

} // namespace svgplayer

#endif // SVG_ANIMATION_CONTROLLER_H
