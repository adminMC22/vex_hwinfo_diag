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

    // Game data snapshot sent to web clients via WebSocket
    struct PlayerData {
        std::string name;
        float health;
        float pos_x, pos_y, pos_z;       // World position
        float bone_x, bone_y;            // Screen-projected head position
        float feet_x, feet_y;            // Screen-projected feet
        bool visible;
        bool is_enemy;
        float distance;
    };

    struct GameSnapshot {
        float camera_x, camera_y, camera_z;
        float camera_yaw, camera_pitch;
        float fov;
        int screen_width, screen_height;
        std::vector<PlayerData> players;
        bool InGame;
    };

    // Web server — serves the control panel page and pushes game data
    // via WebSocket to all connected clients.
    class WebServer {
    public:
        static WebServer& instance() {
            static WebServer ws;
            return ws;
        }

        bool start(int port = 8080);
        void stop();

        void update_snapshot(const GameSnapshot& snap);
        GameSnapshot get_snapshot() const;

        // Config callbacks (from web panel to game engine)
        std::function<void(bool)> on_aimbot_toggle;
        std::function<void(bool)> on_triggerbot_toggle;
        std::function<void(bool)> on_esp_toggle;
        std::function<void(bool)> on_radar_toggle;

    private:
        WebServer() = default;
        ~WebServer() { stop(); }

        void http_thread();
        void ws_thread(SOCKET client);
        void handle_http_request(SOCKET client, const std::string& request);
        void handle_websocket(SOCKET client);
        void broadcast_ws(const std::string& msg);

        SOCKET m_listen_socket = INVALID_SOCKET;
        std::atomic<bool> m_running{ false };
        std::thread m_accept_thread;

        GameSnapshot m_snapshot;
        mutable std::mutex m_snapshot_mutex;

        std::vector<SOCKET> m_ws_clients;
        mutable std::mutex m_clients_mutex;
    };

    inline WebServer& g_web = WebServer::instance();

} // namespace sky::web
