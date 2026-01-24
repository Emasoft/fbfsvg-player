// remote_control.cpp - Cross-platform TCP/JSON remote control implementation
// Uses POSIX sockets on macOS/Linux, Winsock on Windows

#include "remote_control.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <algorithm>

// Platform-specific socket includes
#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef int socklen_t;
    #define CLOSE_SOCKET closesocket
    #define SOCKET_ERROR_CODE WSAGetLastError()
#else
    #include <sys/socket.h>
    #include <sys/select.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <errno.h>
    #define CLOSE_SOCKET close
    #define SOCKET_ERROR_CODE errno
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
#endif

namespace svgplayer {

// Simple JSON parsing helpers (minimal, no external deps)
namespace {

std::string getJsonString(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";

    pos = json.find(':', pos);
    if (pos == std::string::npos) return "";

    // Skip whitespace and find opening quote
    pos = json.find('"', pos);
    if (pos == std::string::npos) return "";

    size_t start = pos + 1;
    size_t end = json.find('"', start);
    if (end == std::string::npos) return "";

    return json.substr(start, end - start);
}

double getJsonNumber(const std::string& json, const std::string& key, double defaultVal = 0.0) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return defaultVal;

    pos = json.find(':', pos);
    if (pos == std::string::npos) return defaultVal;

    // Skip whitespace
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;

    try {
        return std::stod(json.substr(pos));
    } catch (...) {
        return defaultVal;
    }
}

bool getJsonBool(const std::string& json, const std::string& key, bool defaultVal = false) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return defaultVal;

    pos = json.find(':', pos);
    if (pos == std::string::npos) return defaultVal;

    if (json.find("true", pos) != std::string::npos &&
        json.find("true", pos) < json.find(',', pos) &&
        json.find("true", pos) < json.find('}', pos)) {
        return true;
    }
    return false;
}

// Map command strings to enum
RemoteCommand stringToCommand(const std::string& cmd) {
    if (cmd == "play") return RemoteCommand::Play;
    if (cmd == "pause") return RemoteCommand::Pause;
    if (cmd == "stop") return RemoteCommand::Stop;
    if (cmd == "toggle_play") return RemoteCommand::TogglePlay;
    if (cmd == "seek") return RemoteCommand::Seek;
    if (cmd == "set_speed") return RemoteCommand::SetSpeed;
    if (cmd == "fullscreen") return RemoteCommand::Fullscreen;
    if (cmd == "maximize") return RemoteCommand::Maximize;
    if (cmd == "set_position") return RemoteCommand::SetPosition;
    if (cmd == "set_size") return RemoteCommand::SetSize;
    if (cmd == "get_state") return RemoteCommand::GetState;
    if (cmd == "get_stats") return RemoteCommand::GetStats;
    if (cmd == "get_info") return RemoteCommand::GetInfo;
    if (cmd == "screenshot") return RemoteCommand::Screenshot;
    if (cmd == "quit") return RemoteCommand::Quit;
    if (cmd == "ping") return RemoteCommand::Ping;
    if (cmd == "load_file") return RemoteCommand::LoadFile;
    return RemoteCommand::Ping; // Default fallback
}

} // anonymous namespace

// JSON helper implementations
namespace json {

bool parseCommand(const std::string& jsonStr, RemoteCommand& cmd, std::string& params) {
    std::string cmdStr = getJsonString(jsonStr, "cmd");
    if (cmdStr.empty()) return false;

    cmd = stringToCommand(cmdStr);
    params = jsonStr; // Pass full JSON as params for handler to parse
    return true;
}

std::string success(const std::string& data) {
    if (data.empty()) {
        return R"({"status":"ok"})";
    }
    return R"({"status":"ok","result":)" + data + "}";
}

std::string error(const std::string& message) {
    return R"({"status":"error","message":")" + message + "\"}";
}

std::string state(const PlayerState& s) {
    std::ostringstream oss;
    oss << R"({"status":"ok","state":{)";
    oss << R"("playing":)" << (s.playing ? "true" : "false") << ",";
    oss << R"("paused":)" << (s.paused ? "true" : "false") << ",";
    oss << R"("fullscreen":)" << (s.fullscreen ? "true" : "false") << ",";
    oss << R"("maximized":)" << (s.maximized ? "true" : "false") << ",";
    oss << R"("current_frame":)" << s.currentFrame << ",";
    oss << R"("total_frames":)" << s.totalFrames << ",";
    oss << R"("current_time":)" << s.currentTime << ",";
    oss << R"("total_duration":)" << s.totalDuration << ",";
    oss << R"("playback_speed":)" << s.playbackSpeed << ",";
    oss << R"("window_x":)" << s.windowX << ",";
    oss << R"("window_y":)" << s.windowY << ",";
    oss << R"("window_width":)" << s.windowWidth << ",";
    oss << R"("window_height":)" << s.windowHeight << ",";
    oss << R"("loaded_file":")" << s.loadedFile << "\"";
    oss << "}}";
    return oss.str();
}

std::string stats(const PlayerStats& s) {
    std::ostringstream oss;
    oss << R"({"status":"ok","stats":{)";
    oss << R"("fps":)" << s.fps << ",";
    oss << R"("avg_frame_time":)" << s.avgFrameTime << ",";
    oss << R"("avg_render_time":)" << s.avgRenderTime << ",";
    oss << R"("dropped_frames":)" << s.droppedFrames << ",";
    oss << R"("memory_usage":)" << s.memoryUsage << ",";
    oss << R"("elements_rendered":)" << s.elementsRendered;
    oss << "}}";
    return oss.str();
}

} // namespace json

// RemoteControlServer implementation

RemoteControlServer::RemoteControlServer(int port)
    : m_port(port)
    , m_serverSocket(INVALID_SOCKET)
    , m_running(false)
{
#ifdef _WIN32
    // Initialize Winsock
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

RemoteControlServer::~RemoteControlServer() {
    stop();
#ifdef _WIN32
    WSACleanup();
#endif
}

bool RemoteControlServer::start() {
    if (m_running.load()) return true;

    // Create socket
    m_serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (m_serverSocket == INVALID_SOCKET) {
        std::cerr << "RemoteControl: Failed to create socket" << std::endl;
        return false;
    }

    // Allow address reuse
    int opt = 1;
    setsockopt(m_serverSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    // Bind to port
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(m_port);

    if (bind(m_serverSocket, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "RemoteControl: Failed to bind to port " << m_port << std::endl;
        CLOSE_SOCKET(m_serverSocket);
        m_serverSocket = INVALID_SOCKET;
        return false;
    }

    // Start listening
    if (listen(m_serverSocket, 5) == SOCKET_ERROR) {
        std::cerr << "RemoteControl: Failed to listen" << std::endl;
        CLOSE_SOCKET(m_serverSocket);
        m_serverSocket = INVALID_SOCKET;
        return false;
    }

    // Set non-blocking mode
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(m_serverSocket, FIONBIO, &mode);
#else
    int flags = fcntl(m_serverSocket, F_GETFL, 0);
    fcntl(m_serverSocket, F_SETFL, flags | O_NONBLOCK);
#endif

    m_running.store(true);
    m_serverThread = std::thread(&RemoteControlServer::serverThread, this);

    std::cout << "RemoteControl: Server started on port " << m_port << std::endl;
    return true;
}

void RemoteControlServer::stop() {
    if (!m_running.load()) return;

    m_running.store(false);

    // Close all client connections
    {
        std::lock_guard<std::mutex> lock(m_clientsMutex);
        for (int client : m_clients) {
            CLOSE_SOCKET(client);
        }
        m_clients.clear();
    }

    // Close server socket
    if (m_serverSocket != INVALID_SOCKET) {
        CLOSE_SOCKET(m_serverSocket);
        m_serverSocket = INVALID_SOCKET;
    }

    // Wait for thread to finish
    if (m_serverThread.joinable()) {
        m_serverThread.join();
    }

    std::cout << "RemoteControl: Server stopped" << std::endl;
}

void RemoteControlServer::registerHandler(RemoteCommand cmd, CommandCallback callback) {
    std::lock_guard<std::mutex> lock(m_handlersMutex);
    m_handlers[cmd] = callback;
}

void RemoteControlServer::serverThread() {
    while (m_running.load()) {
        // Use select for non-blocking accept with timeout
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(m_serverSocket, &readfds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000; // 100ms timeout

        int selectResult = select(m_serverSocket + 1, &readfds, nullptr, nullptr, &tv);

        if (selectResult > 0 && FD_ISSET(m_serverSocket, &readfds)) {
            // Accept new connection
            struct sockaddr_in clientAddr;
            socklen_t clientLen = sizeof(clientAddr);
            int clientSocket = accept(m_serverSocket, (struct sockaddr*)&clientAddr, &clientLen);

            if (clientSocket != INVALID_SOCKET) {
                std::cout << "RemoteControl: Client connected from "
                          << inet_ntoa(clientAddr.sin_addr) << std::endl;

                // Enable TCP keepalive to detect dead connections
                int keepalive = 1;
                setsockopt(clientSocket, SOL_SOCKET, SO_KEEPALIVE, (const char*)&keepalive, sizeof(keepalive));

                // Set non-blocking mode on client socket for responsive handling
#ifdef _WIN32
                u_long mode = 1;
                ioctlsocket(clientSocket, FIONBIO, &mode);
#else
                int flags = fcntl(clientSocket, F_GETFL, 0);
                fcntl(clientSocket, F_SETFL, flags | O_NONBLOCK);
#endif

                // Add to client list
                {
                    std::lock_guard<std::mutex> lock(m_clientsMutex);
                    m_clients.push_back(clientSocket);
                }
                // NOTE: All client I/O handled in serverThread select() loop below
                // No separate handleClient thread - was causing race condition with duplicate reads
            }
        }

        // Also check existing clients for data
        std::vector<int> clientsCopy;
        {
            std::lock_guard<std::mutex> lock(m_clientsMutex);
            clientsCopy = m_clients;
        }

        for (int client : clientsCopy) {
            fd_set clientfds;
            FD_ZERO(&clientfds);
            FD_SET(client, &clientfds);

            struct timeval clientTv;
            clientTv.tv_sec = 0;
            clientTv.tv_usec = 1000; // 1ms

            if (select(client + 1, &clientfds, nullptr, nullptr, &clientTv) > 0) {
                char buffer[4096];
                int bytesRead = recv(client, buffer, sizeof(buffer) - 1, 0);

                if (bytesRead > 0) {
                    buffer[bytesRead] = '\0';

                    // Process each line (commands are newline-delimited)
                    std::string data(buffer);
                    std::istringstream stream(data);
                    std::string line;

                    while (std::getline(stream, line)) {
                        if (line.empty() || line[0] != '{') continue;

                        // Execute command and send response
                        std::string response = executeCommand(line);
                        response += "\n";
                        // Use MSG_NOSIGNAL on Linux/macOS to prevent SIGPIPE on broken connection
#ifdef _WIN32
                        int sendFlags = 0;
#else
                        int sendFlags = MSG_NOSIGNAL;
#endif
                        ssize_t sent = send(client, response.c_str(), response.size(), sendFlags);
                        if (sent < 0) {
                            // Send failed - client likely disconnected, will be cleaned up on next recv
                            break;
                        }
                    }
                } else if (bytesRead == 0) {
                    // Client disconnected gracefully
                    std::cout << "RemoteControl: Client disconnected" << std::endl;
                    CLOSE_SOCKET(client);

                    std::lock_guard<std::mutex> lock(m_clientsMutex);
                    m_clients.erase(
                        std::remove(m_clients.begin(), m_clients.end(), client),
                        m_clients.end()
                    );
                } else {
                    // bytesRead < 0: check for real error vs EWOULDBLOCK (non-blocking socket)
#ifdef _WIN32
                    int err = WSAGetLastError();
                    if (err != WSAEWOULDBLOCK) {
#else
                    int err = errno;
                    if (err != EAGAIN && err != EWOULDBLOCK) {
#endif
                        // Real error - client connection broken
                        std::cout << "RemoteControl: Client connection error (" << err << ")" << std::endl;
                        CLOSE_SOCKET(client);

                        std::lock_guard<std::mutex> lock(m_clientsMutex);
                        m_clients.erase(
                            std::remove(m_clients.begin(), m_clients.end(), client),
                            m_clients.end()
                        );
                    }
                    // else: EWOULDBLOCK is normal for non-blocking socket with no data
                }
            }
        }
    }
}

void RemoteControlServer::handleClient(int /* clientSocket */) {
    // DEPRECATED: This function is no longer called.
    // All client I/O is now handled in the serverThread select() loop.
    // The previous design with a separate thread per client caused race conditions
    // where both serverThread and handleClient were reading from the same socket,
    // leading to "broken pipe" errors and lost responses.
    //
    // Keeping this function for API compatibility but it does nothing.
}

std::string RemoteControlServer::executeCommand(const std::string& jsonCmd) {
    RemoteCommand cmd;
    std::string params;

    if (!json::parseCommand(jsonCmd, cmd, params)) {
        return json::error("Invalid command format");
    }

    // Look up handler
    CommandCallback handler;
    {
        std::lock_guard<std::mutex> lock(m_handlersMutex);
        auto it = m_handlers.find(cmd);
        if (it == m_handlers.end()) {
            return json::error("Unknown command");
        }
        handler = it->second;
    }

    // Execute handler
    try {
        return handler(params);
    } catch (const std::exception& e) {
        return json::error(std::string("Command failed: ") + e.what());
    }
}

void RemoteControlServer::broadcast(const std::string& message) {
    std::lock_guard<std::mutex> lock(m_clientsMutex);
    std::string msg = message + "\n";

    for (int client : m_clients) {
        send(client, msg.c_str(), msg.size(), 0);
    }
}

int RemoteControlServer::processPendingCommands() {
    // This method allows commands to be queued and processed on the main thread
    // Currently commands are processed directly in the server thread
    // This can be extended if thread-safety becomes an issue
    return 0;
}

} // namespace svgplayer
