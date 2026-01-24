// remote_control.h - Cross-platform remote control interface for SVG Player
// Enables programmatic control via TCP/JSON for automated testing
//
// Protocol: JSON over TCP on configurable port (default: 9999)
// Commands are newline-delimited JSON objects
//
// Example session:
//   Client: {"cmd":"get_state"}\n
//   Server: {"status":"ok","state":{"playing":true,"frame":42,"time":1.75}}\n

#ifndef REMOTE_CONTROL_H
#define REMOTE_CONTROL_H

#include <string>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>
#include <queue>
#include <map>
#include <vector>

// Forward declarations
struct SDL_Window;

namespace svgplayer {

// Command types that can be sent to the player
enum class RemoteCommand {
    // Playback control
    Play,           // Start/resume playback
    Pause,          // Pause playback
    Stop,           // Stop and reset to beginning
    TogglePlay,     // Toggle play/pause
    Seek,           // Seek to specific time (seconds)
    SetSpeed,       // Set playback speed multiplier

    // Window control
    Fullscreen,     // Enter/exit fullscreen
    Maximize,       // Maximize/restore window
    SetPosition,    // Set window position
    SetSize,        // Set window size

    // State queries
    GetState,       // Get current player state
    GetStats,       // Get performance statistics
    GetInfo,        // Get SVG file info

    // Capture
    Screenshot,     // Capture screenshot to file

    // System
    Quit,           // Quit the player
    Ping,           // Health check

    // File operations
    LoadFile,       // Load a new SVG file
};

// Player state structure returned by GetState
struct PlayerState {
    bool playing;
    bool paused;
    bool fullscreen;
    bool maximized;
    int currentFrame;
    int totalFrames;
    double currentTime;
    double totalDuration;
    double playbackSpeed;
    int windowX;
    int windowY;
    int windowWidth;
    int windowHeight;
    std::string loadedFile;
};

// Performance statistics returned by GetStats
struct PlayerStats {
    double fps;
    double avgFrameTime;
    double avgRenderTime;
    int droppedFrames;
    size_t memoryUsage;
    int elementsRendered;
};

// Callback type for command execution
// The player registers handlers for each command type
using CommandCallback = std::function<std::string(const std::string& params)>;

// Remote control server class
class RemoteControlServer {
public:
    // Constructor with optional port (default 9999)
    explicit RemoteControlServer(int port = 9999);
    ~RemoteControlServer();

    // Start/stop the server
    bool start();
    void stop();
    bool isRunning() const { return m_running.load(); }

    // Get the port the server is listening on
    int getPort() const { return m_port; }

    // Register command handlers (called by player)
    void registerHandler(RemoteCommand cmd, CommandCallback callback);

    // Process pending commands (call from main thread)
    // Returns number of commands processed
    int processPendingCommands();

    // Send async notification to all connected clients
    void broadcast(const std::string& message);

private:
    // Server thread function
    void serverThread();

    // Handle a single client connection
    void handleClient(int clientSocket);

    // Parse and execute a command
    std::string executeCommand(const std::string& jsonCmd);

    int m_port;
    int m_serverSocket;
    std::atomic<bool> m_running;
    std::thread m_serverThread;

    // Command handlers registered by the player
    std::map<RemoteCommand, CommandCallback> m_handlers;
    std::mutex m_handlersMutex;

    // Thread-safe command queue for main thread execution
    struct PendingCommand {
        std::string command;
        int clientSocket;
    };
    std::queue<PendingCommand> m_pendingCommands;
    std::mutex m_queueMutex;

    // Connected clients for broadcast
    std::vector<int> m_clients;
    std::mutex m_clientsMutex;
};

// Helper functions for JSON parsing/generation
namespace json {
    // Parse a JSON command string and return command type + params
    bool parseCommand(const std::string& json, RemoteCommand& cmd, std::string& params);

    // Generate JSON response
    std::string success(const std::string& data = "");
    std::string error(const std::string& message);
    std::string state(const PlayerState& state);
    std::string stats(const PlayerStats& stats);
}

} // namespace svgplayer

#endif // REMOTE_CONTROL_H
