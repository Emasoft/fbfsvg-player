// SVGAnimationController.cpp - Shared animation controller implementation
// Extracted from macOS player svg_player_animated.cpp
// See SVGAnimationController.h for class documentation

#include "SVGAnimationController.h"
#include "svg_instrumentation.h"
#include "SVGTypes.h"  // For SVGRenderStats

#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cctype>

namespace svgplayer {

// === SMILAnimation Implementation ===

std::string SMILAnimation::getCurrentValue(double elapsedSeconds) const {
    // Empty values array - return empty string
    if (values.empty()) return "";

    // Zero or negative duration - always return first value
    if (duration <= 0) return values[0];

    // Calculate effective time position
    double t = elapsedSeconds;
    if (repeat) {
        // Looping: wrap time using modulo
        t = fmod(elapsedSeconds, duration);
        // Handle negative modulo result (for reverse playback)
        if (t < 0) t += duration;
    } else if (elapsedSeconds >= duration) {
        // Non-looping and past end: return last value
        return values.back();
    } else if (elapsedSeconds < 0) {
        // Before start: return first value
        return values[0];
    }

    // For discrete calcMode, each value gets equal time slice
    // This is the default for frame-by-frame animation
    double valueTime = duration / static_cast<double>(values.size());
    size_t index = static_cast<size_t>(t / valueTime);

    // Clamp index to valid range (safety check)
    if (index >= values.size()) {
        index = values.size() - 1;
    }

    return values[index];
}

size_t SMILAnimation::getCurrentFrameIndex(double elapsedSeconds) const {
    // Empty values array - return 0
    if (values.empty()) return 0;

    // Zero or negative duration - always return frame 0
    if (duration <= 0) return 0;

    // Calculate effective time position
    double t = elapsedSeconds;
    if (repeat) {
        // Looping: wrap time using modulo
        t = fmod(elapsedSeconds, duration);
        // Handle negative modulo result
        if (t < 0) t += duration;
    } else if (elapsedSeconds >= duration) {
        // Non-looping and past end: return last frame index
        return values.size() - 1;
    } else if (elapsedSeconds < 0) {
        // Before start: return first frame
        return 0;
    }

    // Calculate frame index based on equal time slices
    double valueTime = duration / static_cast<double>(values.size());
    size_t index = static_cast<size_t>(t / valueTime);

    // Clamp index to valid range
    if (index >= values.size()) {
        index = values.size() - 1;
    }

    return index;
}

// === SVGAnimationController Implementation ===

SVGAnimationController::SVGAnimationController()
    : loaded_(false)
    , currentTime_(0.0)
    , duration_(0.0)
    , frameRate_(30.0f)  // Default frame rate
    , totalFrames_(0)
    , playbackState_(PlaybackState::Stopped)
    , repeatMode_(RepeatMode::Loop)  // Default to looping for animations
    , repeatCount_(1)
    , completedLoops_(0)
    , playbackRate_(1.0f)
    , playingForward_(true)
    , scrubbing_(false)
    , stateBeforeScrub_(PlaybackState::Stopped)
    , lastFrameIndex_(0)
{
    // Initialize statistics
    stats_ = {};
    lastUpdateTime_ = std::chrono::steady_clock::now();
}

SVGAnimationController::~SVGAnimationController() {
    unload();
}

// === SVG Content & Parsing ===

bool SVGAnimationController::loadFromContent(const std::string& svgContent) {
    // Unload any existing content first
    unload();

    if (svgContent.empty()) {
        std::cerr << "SVGAnimationController: Cannot load empty content" << std::endl;
        return false;
    }

    // Store original content
    originalContent_ = svgContent;

    // Preprocess SVG (inject synthetic IDs, convert <symbol> to <g>)
    processedContent_ = preprocessSVG(svgContent);

    // Parse animations from preprocessed content
    animations_ = parseAnimations(processedContent_);

    if (animations_.empty()) {
        // No animations found - still valid SVG, just static
        if (verbose_) {
            std::cout << "SVGAnimationController: No SMIL animations found in SVG" << std::endl;
        }
        loaded_ = true;
        duration_ = 0.0;
        totalFrames_ = 1;
        return true;
    }

    // Calculate duration from animations (use longest animation)
    duration_ = 0.0;
    int maxFrames = 0;
    for (const auto& anim : animations_) {
        if (anim.duration > duration_) {
            duration_ = anim.duration;
        }
        if (static_cast<int>(anim.values.size()) > maxFrames) {
            maxFrames = static_cast<int>(anim.values.size());
        }
    }

    // Calculate frame rate from duration and frame count
    totalFrames_ = maxFrames > 0 ? maxFrames : 1;
    if (duration_ > 0 && totalFrames_ > 0) {
        frameRate_ = static_cast<float>(totalFrames_) / static_cast<float>(duration_);
        frameRate_ = std::clamp(frameRate_, 1.0f, 240.0f);  // Reasonable bounds
    } else {
        frameRate_ = 30.0f;  // Fallback default
    }

    // Validate frame rate consistency across animations
    for (const auto& anim : animations_) {
        if (anim.values.size() > 0 && anim.duration > 0) {
            float animFps = static_cast<float>(anim.values.size()) / static_cast<float>(anim.duration);
            if (std::abs(animFps - frameRate_) > 0.1f) {
                std::cerr << "Warning: Animation for " << anim.targetId
                          << " has different frame rate (" << animFps
                          << " vs " << frameRate_ << ")" << std::endl;
            }
        }
    }

    loaded_ = true;

    if (verbose_) {
        std::cout << "SVGAnimationController: Loaded " << animations_.size() << " animations, "
                  << "duration=" << std::fixed << std::setprecision(2) << duration_ << "s, "
                  << "frames=" << totalFrames_ << ", "
                  << "fps=" << std::fixed << std::setprecision(1) << frameRate_ << std::endl;
    }

    return true;
}

bool SVGAnimationController::loadFromFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "SVGAnimationController: Cannot open file: " << filepath << std::endl;
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return loadFromContent(buffer.str());
}

void SVGAnimationController::unload() {
    animations_.clear();
    processedContent_.clear();
    originalContent_.clear();
    syntheticIds_.clear();
    loaded_ = false;

    // Reset timeline
    currentTime_ = 0.0;
    duration_ = 0.0;
    totalFrames_ = 0;

    // Reset playback state
    playbackState_ = PlaybackState::Stopped;
    completedLoops_ = 0;
    playingForward_ = true;

    // Reset scrubbing
    scrubbing_ = false;
    stateBeforeScrub_ = PlaybackState::Stopped;

    // Reset stats
    resetStats();
}

bool SVGAnimationController::isLoaded() const {
    return loaded_;
}

const std::string& SVGAnimationController::getProcessedContent() const {
    return processedContent_;
}

const std::string& SVGAnimationController::getOriginalContent() const {
    return originalContent_;
}

std::string SVGAnimationController::getPreprocessedContent(const std::string& svgContent) {
    // Preprocess the SVG content to inject synthetic IDs into <use> elements
    // without id attributes that contain <animate> children.
    // This allows both DOM parsing and animation extraction to work correctly.
    return preprocessSVG(svgContent);
}

// === Animation Info ===

double SVGAnimationController::getDuration() const {
    return duration_;
}

int SVGAnimationController::getTotalFrames() const {
    return totalFrames_;
}

float SVGAnimationController::getFrameRate() const {
    return frameRate_;
}

const std::vector<SMILAnimation>& SVGAnimationController::getAnimations() const {
    return animations_;
}

bool SVGAnimationController::hasAnimations() const {
    return !animations_.empty();
}

// === Playback Control ===

void SVGAnimationController::play() {
    if (playbackState_ != PlaybackState::Playing) {
        PlaybackState oldState = playbackState_;
        playbackState_ = PlaybackState::Playing;
        lastUpdateTime_ = std::chrono::steady_clock::now();
        notifyStateChange(PlaybackState::Playing);
    }
}

void SVGAnimationController::pause() {
    if (playbackState_ != PlaybackState::Paused) {
        playbackState_ = PlaybackState::Paused;
        notifyStateChange(PlaybackState::Paused);
    }
}

void SVGAnimationController::stop() {
    playbackState_ = PlaybackState::Stopped;
    currentTime_ = 0.0;
    completedLoops_ = 0;
    playingForward_ = true;
    notifyStateChange(PlaybackState::Stopped);
}

void SVGAnimationController::togglePlayback() {
    if (playbackState_ == PlaybackState::Playing) {
        pause();
    } else {
        play();
    }
}

PlaybackState SVGAnimationController::getPlaybackState() const {
    return playbackState_;
}

// === Repeat Mode ===

void SVGAnimationController::setRepeatMode(RepeatMode mode) {
    repeatMode_ = mode;
    if (mode != RepeatMode::Count) {
        repeatCount_ = 0;
    }
}

RepeatMode SVGAnimationController::getRepeatMode() const {
    return repeatMode_;
}

void SVGAnimationController::setRepeatCount(int count) {
    repeatCount_ = std::max(1, count);
    repeatMode_ = RepeatMode::Count;
}

int SVGAnimationController::getRepeatCount() const {
    return repeatCount_;
}

int SVGAnimationController::getCompletedLoops() const {
    return completedLoops_;
}

bool SVGAnimationController::isPlayingForward() const {
    return playingForward_;
}

// === Playback Rate ===

void SVGAnimationController::setPlaybackRate(float rate) {
    // Clamp to reasonable range
    playbackRate_ = std::clamp(rate, -10.0f, 10.0f);
    if (std::abs(playbackRate_) < 0.01f) {
        playbackRate_ = 0.01f;  // Prevent zero rate
    }
}

float SVGAnimationController::getPlaybackRate() const {
    return playbackRate_;
}

// === Timeline ===

bool SVGAnimationController::update(double deltaTime) {
    if (!loaded_ || duration_ <= 0) {
        return false;
    }

    if (playbackState_ != PlaybackState::Playing) {
        return false;
    }

    // Store previous frame for change detection
    size_t previousFrame = lastFrameIndex_;

    // Advance time based on playback rate and direction
    double effectiveDelta = deltaTime * static_cast<double>(playbackRate_);
    if (!playingForward_) {
        effectiveDelta = -effectiveDelta;
    }

    currentTime_ += effectiveDelta;

    // Handle loop behavior based on repeat mode
    handleLoopBehavior();

    // Update statistics
    stats_.animationTimeMs = currentTime_ * 1000.0;
    stats_.currentFrame = getCurrentFrame();
    stats_.totalFrames = totalFrames_;

    // Calculate FPS from update frequency
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - lastUpdateTime_).count();
    if (elapsed > 0) {
        stats_.fps = 1.0 / elapsed;
    }
    lastUpdateTime_ = now;

    // Track per-animation frame changes for dirty region optimization
    // Clear previous frame changes and rebuild
    lastFrameChanges_.clear();

    // Ensure previousFrameIndices_ is sized correctly
    if (previousFrameIndices_.size() != animations_.size()) {
        previousFrameIndices_.resize(animations_.size(), 0);
    }

    // Check each animation for frame changes
    for (size_t i = 0; i < animations_.size(); ++i) {
        size_t currFrame = animations_[i].getCurrentFrameIndex(currentTime_);
        if (currFrame != previousFrameIndices_[i]) {
            // Animation frame changed - record for dirty tracking
            AnimationFrameChange change;
            change.targetId = animations_[i].targetId;
            change.previousFrame = previousFrameIndices_[i];
            change.currentFrame = currFrame;
            lastFrameChanges_.push_back(std::move(change));
        }
        previousFrameIndices_[i] = currFrame;
    }

    // Check if frame changed (global)
    size_t currentFrameIndex = static_cast<size_t>(getCurrentFrame());
    if (currentFrameIndex != previousFrame) {
        lastFrameIndex_ = currentFrameIndex;

        // Invoke instrumentation hook for frame rendered
        // Convert AnimationStats to SVGRenderStats for the hook
        SVGRenderStats renderStats = {};
        renderStats.renderTimeMs = stats_.renderTimeMs;
        renderStats.updateTimeMs = stats_.updateTimeMs;
        renderStats.animationTimeMs = stats_.animationTimeMs;
        renderStats.currentFrame = stats_.currentFrame;
        renderStats.totalFrames = stats_.totalFrames;
        renderStats.fps = stats_.fps;
        renderStats.frameSkips = stats_.frameSkips;
        renderStats.peakMemoryBytes = 0;  // Not tracked in animation stats
        renderStats.elementsRendered = 0;  // Not tracked in animation stats
        SVG_INSTRUMENT_FRAME_RENDERED(renderStats);

        return true;  // Frame changed, needs re-render
    }

    // Also check if any animation changed (for multi-animation sync)
    if (!lastFrameChanges_.empty()) {
        return true;  // At least one animation changed frame
    }

    return false;  // No visual change
}

double SVGAnimationController::getCurrentTime() const {
    return currentTime_;
}

float SVGAnimationController::getProgress() const {
    if (duration_ <= 0) return 0.0f;
    return static_cast<float>(currentTime_ / duration_);
}

int SVGAnimationController::getCurrentFrame() const {
    if (totalFrames_ <= 0 || duration_ <= 0) return 0;

    double frameTime = duration_ / static_cast<double>(totalFrames_);
    int frame = static_cast<int>(currentTime_ / frameTime);

    // Clamp to valid range
    return std::clamp(frame, 0, totalFrames_ - 1);
}

// === Seeking ===

void SVGAnimationController::seekTo(double timeSeconds) {
    currentTime_ = std::clamp(timeSeconds, 0.0, duration_);
    lastFrameIndex_ = static_cast<size_t>(getCurrentFrame());
}

void SVGAnimationController::seekToFrame(int frame) {
    if (totalFrames_ <= 0) return;

    frame = std::clamp(frame, 0, totalFrames_ - 1);
    currentTime_ = timeForFrame(frame);
    lastFrameIndex_ = static_cast<size_t>(frame);
}

void SVGAnimationController::seekToProgress(float progress) {
    progress = std::clamp(progress, 0.0f, 1.0f);
    currentTime_ = static_cast<double>(progress) * duration_;
    lastFrameIndex_ = static_cast<size_t>(getCurrentFrame());
}

void SVGAnimationController::seekToStart() {
    currentTime_ = 0.0;
    lastFrameIndex_ = 0;
}

void SVGAnimationController::seekToEnd() {
    currentTime_ = duration_;
    lastFrameIndex_ = static_cast<size_t>(totalFrames_ > 0 ? totalFrames_ - 1 : 0);
}

// === Frame Stepping ===

void SVGAnimationController::stepForward() {
    stepByFrames(1);
}

void SVGAnimationController::stepBackward() {
    stepByFrames(-1);
}

void SVGAnimationController::stepByFrames(int frames) {
    // Pause playback when stepping
    if (playbackState_ == PlaybackState::Playing) {
        pause();
    }

    int currentFrame = getCurrentFrame();
    int newFrame = currentFrame + frames;

    // Clamp to valid range
    newFrame = std::clamp(newFrame, 0, totalFrames_ - 1);

    seekToFrame(newFrame);
}

// === Relative Seeking ===

void SVGAnimationController::seekForwardByTime(double seconds) {
    seekTo(currentTime_ + seconds);
}

void SVGAnimationController::seekBackwardByTime(double seconds) {
    seekTo(currentTime_ - seconds);
}

void SVGAnimationController::seekForwardByPercentage(float percent) {
    double delta = static_cast<double>(percent) * duration_;
    seekTo(currentTime_ + delta);
}

void SVGAnimationController::seekBackwardByPercentage(float percent) {
    double delta = static_cast<double>(percent) * duration_;
    seekTo(currentTime_ - delta);
}

// === Scrubbing ===

void SVGAnimationController::beginScrubbing() {
    if (!scrubbing_) {
        scrubbing_ = true;
        stateBeforeScrub_ = playbackState_;
        pause();  // Always pause during scrubbing
    }
}

void SVGAnimationController::scrubToProgress(float progress) {
    if (scrubbing_) {
        seekToProgress(progress);
    }
}

void SVGAnimationController::endScrubbing(bool resume) {
    if (scrubbing_) {
        scrubbing_ = false;
        if (resume && stateBeforeScrub_ == PlaybackState::Playing) {
            play();
        }
    }
}

bool SVGAnimationController::isScrubbing() const {
    return scrubbing_;
}

// === Animation State Query ===

std::vector<AnimationState> SVGAnimationController::getCurrentAnimationStates() const {
    std::vector<AnimationState> states;
    states.reserve(animations_.size());

    for (const auto& anim : animations_) {
        AnimationState state;
        state.targetId = anim.targetId;
        state.attributeName = anim.attributeName;
        state.value = anim.getCurrentValue(currentTime_);
        states.push_back(std::move(state));
    }

    return states;
}

std::vector<AnimationFrameChange> SVGAnimationController::getFrameChanges() const {
    // Return copy of frame changes from last update() call
    // Used by DirtyRegionTracker for partial rendering optimization
    return lastFrameChanges_;
}

void SVGAnimationController::updateFrameTracking(double absoluteTime) {
    // Lightweight frame tracking from external absolute time
    // Does NOT modify playback state or trigger callbacks
    // Only updates lastFrameChanges_ for getFrameChanges() retrieval

    lastFrameChanges_.clear();

    // Ensure previousFrameIndices_ is sized correctly
    if (previousFrameIndices_.size() != animations_.size()) {
        previousFrameIndices_.resize(animations_.size(), 0);
    }

    // Check each animation for frame changes at the given time
    for (size_t i = 0; i < animations_.size(); ++i) {
        size_t currFrame = animations_[i].getCurrentFrameIndex(absoluteTime);
        if (currFrame != previousFrameIndices_[i]) {
            // Animation frame changed - record for dirty tracking
            AnimationFrameChange change;
            change.targetId = animations_[i].targetId;
            change.previousFrame = previousFrameIndices_[i];
            change.currentFrame = currFrame;
            lastFrameChanges_.push_back(std::move(change));
        }
        previousFrameIndices_[i] = currFrame;
    }
}

// === Statistics ===

AnimationStats SVGAnimationController::getStats() const {
    return stats_;
}

void SVGAnimationController::resetStats() {
    stats_ = {};
    stats_.totalFrames = totalFrames_;
    lastFrameIndex_ = 0;
}

void SVGAnimationController::updateRenderTime(double timeMs) {
    stats_.renderTimeMs = timeMs;
}

void SVGAnimationController::setVerbose(bool verbose) {
    verbose_ = verbose;
}

// === Utility ===

std::string SVGAnimationController::formatTime(double seconds) {
    int mins = static_cast<int>(seconds) / 60;
    int secs = static_cast<int>(seconds) % 60;
    int ms = static_cast<int>((seconds - std::floor(seconds)) * 1000);

    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << mins << ":"
        << std::setfill('0') << std::setw(2) << secs << "."
        << std::setfill('0') << std::setw(3) << ms;
    return oss.str();
}

int SVGAnimationController::frameForTime(double time) const {
    if (totalFrames_ <= 0 || duration_ <= 0) return 0;

    double frameTime = duration_ / static_cast<double>(totalFrames_);
    int frame = static_cast<int>(time / frameTime);
    return std::clamp(frame, 0, totalFrames_ - 1);
}

double SVGAnimationController::timeForFrame(int frame) const {
    if (totalFrames_ <= 0 || duration_ <= 0) return 0.0;

    frame = std::clamp(frame, 0, totalFrames_ - 1);
    double frameTime = duration_ / static_cast<double>(totalFrames_);
    return static_cast<double>(frame) * frameTime;
}

// === Event Callbacks ===

void SVGAnimationController::setStateChangeCallback(StateChangeCallback callback) {
    stateChangeCallback_ = std::move(callback);
}

void SVGAnimationController::setLoopCallback(LoopCallback callback) {
    loopCallback_ = std::move(callback);
}

void SVGAnimationController::setEndCallback(EndCallback callback) {
    endCallback_ = std::move(callback);
}

// === Private Methods ===

void SVGAnimationController::clampCurrentTime() {
    if (currentTime_ < 0) {
        currentTime_ = 0;
    } else if (currentTime_ > duration_) {
        currentTime_ = duration_;
    }
}

void SVGAnimationController::handleLoopBehavior() {
    if (duration_ <= 0) return;

    switch (repeatMode_) {
        case RepeatMode::None:
            // Play once and stop at end
            if (currentTime_ >= duration_) {
                currentTime_ = duration_;
                pause();
                SVG_INSTRUMENT_ANIMATION_END();  // Instrumentation hook
                if (endCallback_) {
                    endCallback_();
                }
            } else if (currentTime_ < 0) {
                currentTime_ = 0;
                pause();
            }
            break;

        case RepeatMode::Loop:
            // Loop back to start when reaching end (using subtraction for better precision)
            if (currentTime_ >= duration_) {
                while (currentTime_ >= duration_) {
                    currentTime_ -= duration_;
                    completedLoops_++;
                }
                SVG_INSTRUMENT_ANIMATION_LOOP();  // Instrumentation hook
                if (loopCallback_) {
                    loopCallback_(completedLoops_);
                }
            } else if (currentTime_ < 0) {
                while (currentTime_ < 0) {
                    currentTime_ += duration_;
                    completedLoops_++;
                }
                SVG_INSTRUMENT_ANIMATION_LOOP();  // Instrumentation hook
                if (loopCallback_) {
                    loopCallback_(completedLoops_);
                }
            }
            break;

        case RepeatMode::Reverse:
            // Ping-pong: reverse direction at boundaries
            if (currentTime_ >= duration_) {
                currentTime_ = duration_ - (currentTime_ - duration_);
                playingForward_ = false;
                completedLoops_++;
                SVG_INSTRUMENT_ANIMATION_LOOP();  // Instrumentation hook
                if (loopCallback_) {
                    loopCallback_(completedLoops_);
                }
            } else if (currentTime_ < 0) {
                currentTime_ = -currentTime_;
                playingForward_ = true;
                completedLoops_++;
                SVG_INSTRUMENT_ANIMATION_LOOP();  // Instrumentation hook
                if (loopCallback_) {
                    loopCallback_(completedLoops_);
                }
            }
            break;

        case RepeatMode::Count:
            // Repeat specified number of times
            if (currentTime_ >= duration_) {
                completedLoops_++;
                if (completedLoops_ >= repeatCount_) {
                    currentTime_ = duration_;
                    pause();
                    SVG_INSTRUMENT_ANIMATION_END();  // Instrumentation hook
                    if (endCallback_) {
                        endCallback_();
                    }
                } else {
                    currentTime_ = fmod(currentTime_, duration_);
                    SVG_INSTRUMENT_ANIMATION_LOOP();  // Instrumentation hook
                    if (loopCallback_) {
                        loopCallback_(completedLoops_);
                    }
                }
            }
            break;
    }
}

void SVGAnimationController::notifyStateChange(PlaybackState newState) {
    if (stateChangeCallback_) {
        stateChangeCallback_(newState);
    }
}

// === Parsing Methods (extracted from macOS player) ===

double SVGAnimationController::parseDuration(const std::string& durStr) {
    if (durStr.empty()) return 0;

    double value = 0;
    std::string unit;

    // Find where the numeric part ends
    size_t i = 0;
    while (i < durStr.size() && (std::isdigit(durStr[i]) || durStr[i] == '.' || durStr[i] == '-')) {
        i++;
    }

    // Parse the numeric value
    try {
        value = std::stod(durStr.substr(0, i));
    } catch (const std::exception& e) {
        std::cerr << "Warning: Failed to parse duration '" << durStr << "': " << e.what() << std::endl;
        return 0;
    }

    // Get the unit suffix
    unit = durStr.substr(i);

    // Convert to seconds based on unit
    if (unit == "ms") {
        return value / 1000.0;
    } else if (unit == "s" || unit.empty()) {
        return value;
    } else if (unit == "min") {
        return value * 60.0;
    } else if (unit == "h") {
        return value * 3600.0;
    }

    return value;  // Default to seconds
}

std::string SVGAnimationController::extractAttribute(const std::string& tag, const std::string& attrName) {
    // Build search pattern: attrName="
    std::string searchStr = attrName + "=\"";
    size_t start = tag.find(searchStr);
    if (start == std::string::npos) {
        // Try single quotes: attrName='
        searchStr = attrName + "='";
        start = tag.find(searchStr);
        if (start == std::string::npos) {
            return "";
        }
    }

    // Move past the attribute name and opening quote
    start += searchStr.length();

    // Find the closing quote (matching the opening quote type)
    char quoteChar = tag[start - 1];  // Either " or '
    size_t end = tag.find(quoteChar, start);
    if (end == std::string::npos) return "";

    return tag.substr(start, end - start);
}

size_t SVGAnimationController::findLastOf(const std::string& str, const std::string& pattern, size_t endPos) {
    size_t lastPos = std::string::npos;
    size_t pos = 0;
    while ((pos = str.find(pattern, pos)) != std::string::npos && pos < endPos) {
        lastPos = pos;
        pos++;
    }
    return lastPos;
}

std::string SVGAnimationController::convertSymbolsToGroups(const std::string& content) {
    std::string result = content;

    // Replace <symbol> tags with <g> tags
    // Skia doesn't support <symbol> elements, but <g> works similarly for our purposes
    size_t pos = 0;
    while ((pos = result.find("<symbol", pos)) != std::string::npos) {
        // Find the end of the opening tag
        size_t tagEnd = result.find(">", pos);
        if (tagEnd == std::string::npos) break;

        // Check if it's self-closing
        bool selfClosing = (result[tagEnd - 1] == '/');

        // Replace <symbol with <g
        result.replace(pos, 7, "<g");

        // If not self-closing, also replace the closing tag </symbol>
        if (!selfClosing) {
            size_t closePos = result.find("</symbol>", pos);
            if (closePos != std::string::npos) {
                result.replace(closePos, 9, "</g>");
            }
        }

        pos += 2;  // Move past "<g"
    }

    return result;
}

std::string SVGAnimationController::preprocessSVG(const std::string& content) {
    // First convert <symbol> to <g> since Skia doesn't support <symbol>
    std::string result = convertSymbolsToGroups(content);

    int syntheticIdCounter = 0;
    size_t searchPos = 0;

    // Find all <use> elements that contain <animate> but don't have an id
    // These need synthetic IDs injected so animations can target them
    while ((searchPos = result.find("<use", searchPos)) != std::string::npos) {
        // Find the end of this <use> tag
        size_t tagEnd = result.find(">", searchPos);
        if (tagEnd == std::string::npos) break;

        std::string useTag = result.substr(searchPos, tagEnd - searchPos + 1);

        // Check if this <use> already has an id attribute
        bool hasId = (useTag.find(" id=") != std::string::npos ||
                      useTag.find("\tid=") != std::string::npos ||
                      useTag.find("\nid=") != std::string::npos);

        if (!hasId) {
            // Check if there's an <animate> between this <use> and its closing </use>
            size_t closeUsePos = result.find("</use>", tagEnd);
            size_t nextUsePos = result.find("<use", tagEnd + 1);
            size_t animatePos = result.find("<animate", tagEnd);

            bool hasAnimateChild = false;
            if (animatePos != std::string::npos) {
                if (closeUsePos != std::string::npos && animatePos < closeUsePos) {
                    hasAnimateChild = true;
                } else if (closeUsePos == std::string::npos &&
                           (nextUsePos == std::string::npos || animatePos < nextUsePos)) {
                    hasAnimateChild = true;
                }
            }

            if (hasAnimateChild) {
                // Inject a synthetic ID into this <use> element
                std::string syntheticId = "_smil_target_" + std::to_string(syntheticIdCounter++);

                // Insert id="syntheticId" after "<use"
                size_t insertPos = searchPos + 4;  // After "<use"
                std::string toInsert = " id=\"" + syntheticId + "\"";
                result.insert(insertPos, toInsert);

                // Store the mapping
                syntheticIds_[searchPos] = syntheticId;

                std::cout << "SVGAnimationController: Injected synthetic ID '" << syntheticId
                          << "' into <use> element" << std::endl;

                // Adjust searchPos to account for inserted text
                searchPos = tagEnd + toInsert.length() + 1;
                continue;
            }
        }

        searchPos = tagEnd + 1;
    }

    return result;
}

std::vector<SMILAnimation> SVGAnimationController::parseAnimations(const std::string& content) {
    std::vector<SMILAnimation> animations;

    // Find all <animate> tags
    std::string animateStart = "<animate";
    size_t pos = 0;
    int animateTagsFound = 0;

    while ((pos = content.find(animateStart, pos)) != std::string::npos) {
        animateTagsFound++;
        // Find the end of this <animate> tag
        size_t tagEnd = content.find(">", pos);
        if (tagEnd == std::string::npos) break;

        // Handle self-closing tags: <animate ... />
        if (content[tagEnd - 1] == '/') {
            tagEnd--;
        }

        std::string animateTag = content.substr(pos, tagEnd - pos + 1);

        SMILAnimation anim;

        // Extract attributeName (e.g., "xlink:href", "opacity", etc.)
        anim.attributeName = extractAttribute(animateTag, "attributeName");

        // Extract values and split by semicolon
        std::string valuesStr = extractAttribute(animateTag, "values");
        if (!valuesStr.empty()) {
            std::stringstream ss(valuesStr);
            std::string value;
            while (std::getline(ss, value, ';')) {
                // Trim leading and trailing whitespace (critical for proper frame matching)
                size_t start = value.find_first_not_of(" \t\n\r");
                size_t end = value.find_last_not_of(" \t\n\r");
                if (start != std::string::npos && end != std::string::npos) {
                    std::string trimmed = value.substr(start, end - start + 1);
                    if (!trimmed.empty()) {
                        anim.values.push_back(trimmed);
                    }
                }
            }
        }

        // Extract duration
        std::string durStr = extractAttribute(animateTag, "dur");
        if (!durStr.empty()) {
            anim.duration = parseDuration(durStr);
        }

        // Extract repeatCount
        std::string repeatStr = extractAttribute(animateTag, "repeatCount");
        if (!repeatStr.empty()) {
            anim.repeat = (repeatStr == "indefinite");
            if (!anim.repeat && !repeatStr.empty()) {
                try {
                    anim.repeat = (std::stod(repeatStr) > 1);
                } catch (const std::exception& e) {
                    std::cerr << "Warning: Failed to parse repeatCount '" << repeatStr << "': " << e.what() << std::endl;
                }
            }
        }

        // Extract calcMode (discrete, linear, paced, spline)
        anim.calcMode = extractAttribute(animateTag, "calcMode");
        if (anim.calcMode.empty()) {
            anim.calcMode = "discrete";  // Default for frame-by-frame animation
        }

        // Find the target element for this <animate>
        // First check for xlink:href="#target" or href="#target" on the animate element itself
        // This handles standalone animations like: <animate xlink:href="#loadRing" .../>
        std::string animXlinkHref = extractAttribute(animateTag, "xlink:href");
        std::string animHref = extractAttribute(animateTag, "href");
        std::string hrefTarget = animXlinkHref.empty() ? animHref : animXlinkHref;

        // If href points to an element ID (starts with #), extract the ID
        if (!hrefTarget.empty() && hrefTarget[0] == '#') {
            anim.targetId = hrefTarget.substr(1);  // Strip the leading #
        }

        // If no href target, look backwards for parent element with an id
        if (anim.targetId.empty()) {
            std::string before = content.substr(0, pos);

            // Find the closest <use> element containing this <animate>
            size_t lastUsePos = findLastOf(before, "<use", before.length());

            // Make sure the <use> wasn't closed before our <animate>
            if (lastUsePos != std::string::npos) {
                size_t useCloseAfterUse = before.find("</use>", lastUsePos);
                if (useCloseAfterUse != std::string::npos) {
                    // The <use> was closed before our <animate>, so it's not our parent
                    lastUsePos = std::string::npos;
                }
            }

            size_t parentPos = std::string::npos;

            if (lastUsePos != std::string::npos) {
                // Prefer <use> as the direct parent for xlink:href animations
                parentPos = lastUsePos;
            } else {
                // Fall back to <g> if no <use> found
                size_t lastGPos = findLastOf(before, "<g ", before.length());
                parentPos = lastGPos;
            }

            // Extract the id from the parent element
            if (parentPos != std::string::npos) {
                size_t parentTagEnd = before.find(">", parentPos);
                if (parentTagEnd != std::string::npos) {
                    std::string parentTag = before.substr(parentPos, parentTagEnd - parentPos);
                    anim.targetId = extractAttribute(parentTag, "id");
                }
            }
        }

        // Only add animation if it has values and a target
        if (!anim.values.empty() && !anim.targetId.empty()) {
            animations.push_back(anim);
            if (verbose_) {
                std::cout << "SVGAnimationController: Found animation - target='" << anim.targetId
                          << "', attr='" << anim.attributeName
                          << "', frames=" << anim.values.size()
                          << ", duration=" << std::fixed << std::setprecision(4) << anim.duration << "s"
                          << ", mode='" << anim.calcMode << "'" << std::endl;
            }
        } else if (animateTagsFound <= 20 && verbose_) {
            // Debug: show why animation was rejected (limit to first 20 to avoid spam)
            std::cout << "DEBUG: Rejected animate tag #" << animateTagsFound
                      << " - values empty=" << anim.values.empty()
                      << ", targetId empty=" << anim.targetId.empty()
                      << ", attr='" << anim.attributeName << "'" << std::endl;
        }

        pos = tagEnd + 1;
    }

    if (animateTagsFound > 0 && animations.empty() && verbose_) {
        std::cout << "DEBUG: Parsed " << animateTagsFound << " <animate> tags but none had valid target+values" << std::endl;
    }

    return animations;
}

} // namespace svgplayer
