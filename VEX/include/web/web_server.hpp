#pragma once

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>
#include <cstdint>
#include <WinSock2.h>
#include <WS2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

namespace sky::web {

struct PlayerEntry {
    std::string name;
    int health = 0;
    float distance = 0;
    float pos_x = 0, pos_y = 0, pos_z = 0;
    bool is_enemy = false;
};

struct GameSnapshot {
    float camera_x = 0, camera_y = 0, camera_z = 0;
    std::vector<PlayerEntry> players;
    bool InGame = false;
};

class WebServer {
public:
    WebServer() = default;
    ~WebServer() { stop(); }

    bool start(int port);
    void stop();
    void update_snapshot(const GameSnapshot& snap);
    GameSnapshot get_snapshot() const;
    void broadcast_ws(const std::string& msg);

    // Callbacks from web panel controls
    std::function<void(bool)> on_esp_toggle;
    std::function<void(bool)> on_radar_toggle;
    std::function<void(bool)> on_aimbot_toggle;
    std::function<void(bool)> on_triggerbot_toggle;

    std::atomic<bool> running{false};

    // Shared state (public so handle_client can access)
    std::mutex client_mutex;
    std::vector<SOCKET> ws_clients;
    std::mutex snapshot_mutex;
    GameSnapshot m_snapshot;

private:
    SOCKET m_listen_socket = INVALID_SOCKET;
    std::thread m_accept_thread;
};

} // namespace sky::web