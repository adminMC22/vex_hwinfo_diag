#include "../../include/web/web_server.hpp"
#include "../../include/utils/logger.hpp"
#include <sstream>
#include <cstring>
#include <algorithm>

// Minimal SHA-1 for WebSocket handshake
// (inline implementation to avoid OpenSSL dependency)
static void sha1(const uint8_t* data, size_t len, uint8_t out[20]) {
    uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;
    uint32_t w[80];
    size_t i;
    for (i = 0; i < len; i++) {
        ((uint8_t*)w)[i] = data[i];
    }
    // Pad
    ((uint8_t*)w)[len] = 0x80;
    i = (len + 1);
    while (i % 64 != 56) { ((uint8_t*)w)[i] = 0; i++; }
    // Length in bits (big-endian) at offset 56-63
    uint64_t bitlen = (uint64_t)len * 8;
    w[14] = (uint32_t)(bitlen >> 32);
    w[15] = (uint32_t)(bitlen);

    for (int j = 16; j < 80; j++) {
        uint32_t a = w[j-3] ^ w[j-8] ^ w[j-14] ^ w[j-16];
        w[j] = (a << 1) | (a >> 31);
    }

    uint32_t a = h0, b = h1, c = h2, dIdx = h3, e = h4;
    for (int j = 0; j < 80; j++) {
        uint32_t f, k;
        if (j < 20) { f = (b & c) | (~b & dIdx); k = 0x5A827999; }
        else if (j < 40) { f = b ^ c ^ dIdx; k = 0x6ED9EBA1; }
        else if (j < 60) { f = (b & c) | (b & dIdx) | (c & dIdx); k = 0x8F1BBCDC; }
        else { f = b ^ c ^ dIdx; k = 0xCA62C1D6; }

        uint32_t temp = (a << 5) | (a >> 27);
        temp += f + e + k + w[j];
        e = dIdx; dIdx = c; c = (b << 30) | (b >> 2); b = a; a = temp;
    }
    h0 += a; h1 += b; h2 += c; h3 += dIdx; h4 += e;

    // Big-endian output
    for (int j = 0; j < 4; j++) {
        out[j]      = (h0 >> (24 - j*8)) & 0xFF;
        out[j+4]    = (h1 >> (24 - j*8)) & 0xFF;
        out[j+8]    = (h2 >> (24 - j*8)) & 0xFF;
        out[j+12]   = (h3 >> (24 - j*8)) & 0xFF;
        out[j+16]   = (h4 >> (24 - j*8)) & 0xFF;
    }
}

// Base64 encode (for WebSocket handshake)
static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static std::string base64_encode(const uint8_t* data, size_t len) {
    std::string result;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = (uint32_t)data[i] << 16;
        if (i + 1 < len) n |= (uint32_t)data[i+1] << 8;
        if (i + 2 < len) n |= data[i+2];
        result += b64_table[(n >> 18) & 63];
        result += b64_table[(n >> 12) & 63];
        result += (i + 1 < len) ? b64_table[(n >> 6) & 63] : '=';
        result += (i + 2 < len) ? b64_table[n & 63] : '=';
    }
    return result;
}

// WebSocket frame helpers
static std::string ws_frame(const std::string& payload) {
    std::string frame;
    frame.push_back((char)0x81); // FIN + text
    if (payload.size() < 126) {
        frame.push_back((char)payload.size());
    } else if (payload.size() <= 65535) {
        frame.push_back((char)126);
        frame.push_back((char)((payload.size() >> 8) & 0xFF));
        frame.push_back((char)(payload.size() & 0xFF));
    } else {
        frame.push_back((char)127);
        for (int i = 7; i >= 0; i--)
            frame.push_back((char)((payload.size() >> (i*8)) & 0xFF));
    }
    frame += payload;
    return frame;
}

static std::string ws_unframe(const std::string& data) {
    if (data.size() < 2) return "";
    uint8_t opcode = data[0] & 0x0F;
    bool masked = data[1] & 0x80;
    uint64_t len = data[1] & 0x7F;
    size_t header = 2;
    if (len == 126) { len = (data[2] << 8) | data[3]; header = 4; }
    else if (len == 127) { header = 10; }
    if (masked) header += 4;
    if (data.size() < header + len) return "";
    std::string payload(data.begin() + header, data.begin() + header + len);
    if (masked) {
        uint8_t mask[4] = { (uint8_t)data[header-4], (uint8_t)data[header-3], (uint8_t)data[header-2], (uint8_t)data[header-1] };
        for (size_t i = 0; i < payload.size(); i++)
            payload[i] ^= mask[i % 4];
    }
    return payload;
}

// JSON escape helper
static std::string json_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c;
        }
    }
    return out;
}

// ================================================================
// Embedded HTML web page
// ================================================================
static const char* HTML_PAGE = R"HTML(<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Sky Panel</title>
<style>
*{margin:0;padding:0;box-sizing:border-box;font-family:'Segoe UI',sans-serif}
body{background:#0a0a0f;color:#e0e0e0;display:flex;min-height:100vh}
.sidebar{width:240px;background:#12121a;padding:15px;border-right:1px solid #333}
.sidebar h1{font-size:18px;color:#7c9eff;margin-bottom:15px}
.sidebar .status{padding:8px;border-radius:4px;margin-bottom:10px;font-size:12px;text-align:center}
.status.connected{background:#1a3326;color:#4ade80}
.status.disconnected{background:#331a1a;color:#f87171}
.sidebar .section{margin-top:15px}
.sidebar .section h2{font-size:12px;color:#888;text-transform:uppercase;margin-bottom:8px}
.sidebar .toggle{display:flex;align-items:center;justify-content:space-between;padding:8px 0;cursor:pointer}
.sidebar .toggle span{font-size:13px}
.sidebar .toggle input{width:18px;height:18px}
.main{flex:1;padding:20px;display:flex;gap:20px}
.panel{background:#12121a;border-radius:8px;padding:15px;flex:1}
.panel h2{font-size:14px;color:#7c9eff;margin-bottom:10px}
#radar{width:400px;height:400px;border-radius:4px;background:#0c0c14;border:1px solid #333}
#esp{width:100%;height:500px;border-radius:4px;background:#0c0c14;border:1px solid #333}
.stats{display:flex;gap:15px;margin-bottom:15px}
.stat{background:#1a1a24;padding:10px 15px;border-radius:4px;min-width:100px}
.stat .label{font-size:10px;color:#888;text-transform:uppercase}
.stat .value{font-size:20px;color:#4ade80}
</style>
</head>
<body>
<div class="sidebar">
<h1>Sky Panel</h1>
<div class="status disconnected" id="status">Disconnected</div>
<div class="section">
<h2>Features</h2>
<div class="toggle"><span>ESP</span><input type="checkbox" id="esp_toggle"></div>
<div class="toggle"><span>Radar</span><input type="checkbox" id="radar_toggle" checked></div>
<div class="toggle"><span>Aimbot</span><input type="checkbox" id="aimbot_toggle"></div>
<div class="toggle"><span>Triggerbot</span><input type="checkbox" id="trigger_toggle"></div>
</div>
<div class="section">
<h2>Info</h2>
<div style="font-size:11px;color:#888">
<div>Players: <span id="player_count">0</span></div>
<div>FPS: <span id="fps">0</span></div>
<div>In Game: <span id="in_game">No</span></div>
</div>
</div>
</div>
<div class="main">
<div class="panel" style="flex:1">
<h2>Radar (Top-Down)</h2>
<canvas id="radar" width="400" height="400"></canvas>
</div>
<div class="panel" style="flex:1">
<h2>Player List</h2>
<div id="players"></div>
</div>
</div>
<script>
let ws;
let connected=false;
function connect(){
ws=new WebSocket('ws://'+location.host+'/ws');
ws.onopen=()=>{connected=true;document.getElementById('status').textContent='Connected';document.getElementById('status').className='status connected';};
ws.onclose=()=>{connected=false;document.getElementById('status').textContent='Disconnected';document.getElementById('status').className='status disconnected';setTimeout(connect,1000);};
ws.onmessage=(e)=>{update(JSON.parse(e.data));};
ws.onerror=()=>{ws.close();};
}
connect();

function sendCmd(key,val){
if(!ws||ws.readyState!=1)return;
ws.send(JSON.stringify({type:'cmd',key:key,value:val}));
}

document.getElementById('esp_toggle').onchange=e=>sendCmd('esp',e.target.checked);
document.getElementById('radar_toggle').onchange=e=>sendCmd('radar',e.target.checked);
document.getElementById('aimbot_toggle').onchange=e=>sendCmd('aimbot',e.target.checked);
document.getElementById('trigger_toggle').onchange=e=>sendCmd('trigger',e.target.checked);

function update(data){
document.getElementById('player_count').textContent=data.players.length;
document.getElementById('in_game').textContent=data.InGame?'Yes':'No';

// Radar
const canvas=document.getElementById('radar');
const ctx=canvas.getContext('2d');
ctx.clearRect(0,0,400,400);
ctx.fillStyle='#0c0c14';
ctx.fillRect(0,0,400,400);

if(data.InGame&&data.players.length>0){
const cx=200,cy=200;scale=1.5;
// Draw players
for(const p of data.players){
const dx=p.pos_x-data.camera_x;
const dy=p.pos_y-data.camera_y;
const px=cx+dy*scale;
const py=cy-dx*scale;
if(px<0||px>400||py<0||py>400)continue;

ctx.fillStyle=p.is_enemy?'#ff4444':'#44ff44';
ctx.beginPath();
ctx.arc(px,py,4,0,Math.PI*2);
ctx.fill();

ctx.fillStyle='#fff';
ctx.font='10px sans-serif';
ctx.fillText(p.name+Math.round(p.distance)+'m',px+6,py+3);
}

// Draw self (center)
ctx.fillStyle='#4488ff';
ctx.beginPath();
ctx.arc(cx,cy,5,0,Math.PI*2);
ctx.fill();
}

// Player list
const list=document.getElementById('players');
list.innerHTML='';
for(const p of data.players){
const d=document.createElement('div');
d.style.cssText='padding:5px;border-bottom:1px solid #333;font-size:12px;';
const hp=Math.round(p.health);
const hcolor=hp>50?'#4ade80':hp>0?''#facc15':'#f87171';
d.innerHTML='<span style="color:'+(p.is_enemy?'#f87171':'#4ade80')+'">'+p.name+'</span> <span style="color:'+hcolor+'">HP:'+hp+'</span> <span style="color:#888">'+Math.round(p.distance)+'m</span>';
list.appendChild(d);
}
}
</script>
</body>
</html>)HTML";

namespace sky::web {

bool WebServer::start(int port) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        LOG_ERROR("WSAStartup failed");
        return false;
    }

    m_listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_listen_socket == INVALID_SOCKET) {
        LOG_ERROR("socket() failed");
        return false;
    }

    // Allow address reuse
    BOOL opt = TRUE;
    setsockopt(m_listen_socket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY); // 0.0.0.0 for phone access
    addr.sin_port = htons(port);

    if (bind(m_listen_socket, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        LOG_ERROR("bind() failed: " + std::to_string(WSAGetLastError()));
        closesocket(m_listen_socket);
        return false;
    }

    if (listen(m_listen_socket, 10) == SOCKET_ERROR) {
        LOG_ERROR("listen() failed");
        closesocket(m_listen_socket);
        return false;
    }

    m_running = true;
    m_accept_thread = std::thread(&WebServer::http_thread, this);

    LOG_INFO("Web server started on port " + std::to_string(port));
    LOG_INFO("Panel URL: http://localhost:" + std::to_string(port));
    LOG_INFO("Phone URL: http://<your-PC-IP>:" + std::to_string(port));
    return true;
}

void WebServer::stop() {
    m_running = false;
    if (m_listen_socket != INVALID_SOCKET) {
        closesocket(m_listen_socket);
        m_listen_socket = INVALID_SOCKET;
    }
    if (m_accept_thread.joinable()) m_accept_thread.join();
    WSACleanup();
}

void WebServer::http_thread() {
    while (m_running) {
        SOCKET client = accept(m_listen_socket, nullptr, nullptr);
        if (client == INVALID_SOCKET) continue;

        // Set timeout (long enough for browser to send request)
        DWORD timeout = 30000;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

        // Read request
        char buf[4096] = {};
        int received = recv(client, buf, sizeof(buf) - 1, 0);
        if (received <= 0) { closesocket(client); continue; }

        std::string request(buf, received);

        LOG_INFO("HTTP request received: " + request.substr(0, 100));

        // Check if WebSocket upgrade (case-insensitive)
        std::string request_lower;
        for (char c : request) request_lower += (char)tolower((unsigned char)c);
        bool is_ws = request_lower.find("upgrade: websocket") != std::string::npos;

        if (is_ws) {
            LOG_INFO("WebSocket upgrade detected");
            handle_websocket(client, request);
        } else {
            LOG_INFO("HTTP request (serving page)");
            handle_http_request(client, request);
        }
    }
}

void WebServer::handle_http_request(SOCKET client, const std::string& request) {
    std::string response = "HTTP/1.1 200 OK\r\n";
    response += "Content-Type: text/html; charset=UTF-8\r\n";
    response += "Connection: close\r\n";
    response += "Access-Control-Allow-Origin: *\r\n";
    response += "\r\n";
    response += HTML_PAGE;

    send(client, response.c_str(), (int)response.size(), 0);
    closesocket(client);
}

void WebServer::handle_websocket(SOCKET client, const std::string& request) {
    // Parse Sec-WebSocket-Key (case-insensitive search)
    std::string req_lower;
    for (char c : request) req_lower += (char)tolower((unsigned char)c);
    size_t key_pos = req_lower.find("sec-websocket-key: ");
    if (key_pos == std::string::npos) { closesocket(client); return; }
    key_pos += 19;  // Length of "sec-websocket-key: "
    size_t key_end = request.find("\r\n", key_pos);
    std::string ws_key = request.substr(key_pos, key_end - key_pos);
    std::string accept_key = ws_key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

    uint8_t sha1_hash[20];
    sha1((const uint8_t*)accept_key.c_str(), accept_key.size(), sha1_hash);
    std::string encoded = base64_encode(sha1_hash, 20);

    std::string response = "HTTP/1.1 101 Switching Protocols\r\n";
    response += "Upgrade: websocket\r\n";
    response += "Connection: Upgrade\r\n";
    response += "Sec-WebSocket-Accept: " + encoded + "\r\n";
    response += "\r\n";

    if (send(client, response.c_str(), (int)response.size(), 0) == SOCKET_ERROR) {
        closesocket(client);
        return;
    }

    // Add to clients list
    {
        std::lock_guard<std::mutex> lock(m_clients_mutex);
        m_ws_clients.push_back(client);
        LOG_INFO("WebSocket client connected (total: " + std::to_string(m_ws_clients.size()) + ")");
    }

    // Send initial snapshot
    {
        std::lock_guard<std::mutex> lock(m_snapshot_mutex);
        std::string json = "{";
        json += "\"InGame\":false,\"players\":[]";
        json += "}";
        std::string frame = ws_frame(json);
        send(client, frame.c_str(), (int)frame.size(), 0);
    }

    // Listen for commands
    while (m_running) {
        char rbuf[1024] = {};
        int r = recv(client, rbuf, sizeof(rbuf), 0);
        if (r <= 0) break;

        std::string data(rbuf, r);
        std::string msg = ws_unframe(data);
        if (msg.empty()) continue;

        // Parse commands
        if (msg.find("\"type\":\"cmd\"") != std::string::npos) {
            bool val = msg.find("\"value\":true") != std::string::npos;
            if (msg.find("\"key\":\"esp\"") != std::string::npos && on_esp_toggle) on_esp_toggle(val);
            else if (msg.find("\"key\":\"radar\"") != std::string::npos && on_radar_toggle) on_radar_toggle(val);
            else if (msg.find("\"key\":\"aimbot\"") != std::string::npos && on_aimbot_toggle) on_aimbot_toggle(val);
            else if (msg.find("\"key\":\"trigger\"") != std::string::npos && on_triggerbot_toggle) on_triggerbot_toggle(val);
            LOG_INFO("Web cmd: " + msg);
        }
    }

    // Remove from clients
    {
        std::lock_guard<std::mutex> lock(m_clients_mutex);
        m_ws_clients.erase(std::remove(m_ws_clients.begin(), m_ws_clients.end(), client), m_ws_clients.end());
    }
    closesocket(client);
    LOG_INFO("WebSocket client disconnected");
}

void WebServer::update_snapshot(const GameSnapshot& snap) {
    std::lock_guard<std::mutex> lock(m_snapshot_mutex);
    m_snapshot = snap;

    // Build JSON
    std::string json = "{";
    json += "\"InGame\":" + std::string(snap.InGame ? "true" : "false") + ",";
    json += "\"camera\":{\"x\":" + std::to_string(snap.camera_x) + ",\"y\":" + std::to_string(snap.camera_y) + ",\"z\":" + std::to_string(snap.camera_z) + "},";
    json += "\"players\":[";
    for (size_t i = 0; i < snap.players.size(); i++) {
        auto& p = snap.players[i];
        if (i > 0) json += ",";
        json += "{\"name\":\"" + json_escape(p.name) + "\",";
        json += "\"health\":" + std::to_string(p.health) + ",";
        json += "\"pos_x\":" + std::to_string(p.pos_x) + ",\"pos_y\":" + std::to_string(p.pos_y) + ",\"pos_z\":" + std::to_string(p.pos_z) + ",";
        json += "\"is_enemy\":" + std::string(p.is_enemy ? "true" : "false") + ",";
        json += "\"distance\":" + std::to_string(p.distance) + ",";
        json += "\"visible\":" + std::string(p.visible ? "true" : "false");
        json += "}";
    }
    json += "]}";

    broadcast_ws(json);
}

GameSnapshot WebServer::get_snapshot() const {
    std::lock_guard<std::mutex> lock(m_snapshot_mutex);
    return m_snapshot;
}

void WebServer::broadcast_ws(const std::string& msg) {
    std::lock_guard<std::mutex> lock(m_clients_mutex);
    std::string frame = ws_frame(msg);
    for (auto it = m_ws_clients.begin(); it != m_ws_clients.end();) {
        if (send(*it, frame.c_str(), (int)frame.size(), 0) == SOCKET_ERROR) {
            closesocket(*it);
            it = m_ws_clients.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace sky::web
