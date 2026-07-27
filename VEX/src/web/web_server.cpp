#include "../../include/web/web_server.hpp"
#include "../../include/utils/logger.hpp"
#include <sstream>
#include <cstring>
#include <algorithm>
#include <thread>

// ===============================================================
// Minimal SHA-1 for WebSocket handshake
// ================================================================
static void sha1_hash(const uint8_t* input, size_t len, uint8_t output[20]) {
    uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;
    size_t new_len = (((len + 9 + 63) / 64) * 64) - 1;
    uint8_t* msg = new uint8_t[new_len + 1];
    memcpy(msg, input, len);
    msg[len] = 0x80;
    memset(msg + len + 1, 0, new_len - len - 1);
    msg[new_len - 3] = (uint8_t)((len * 8) >> 24);
    msg[new_len - 2] = (uint8_t)((len * 8) >> 16);
    msg[new_len - 1] = (uint8_t)((len * 8) >> 8);
    msg[new_len]     = (uint8_t)((len * 8) & 0xFF);
    for (size_t i = 0; i <= new_len; i += 64) {
        uint32_t w[80];
        for (int t = 0; t < 16; t++)
            w[t] = (uint32_t)msg[i + t * 4] << 24 | (uint32_t)msg[i + t * 4 + 1] << 16 |
                   (uint32_t)msg[i + t * 4 + 2] << 8 | msg[i + t * 4 + 3];
        for (int t = 16; t < 80; t++)
            w[t] = _rotl(w[t - 3] ^ w[t - 8] ^ w[t - 14] ^ w[t - 16], 1);
        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int t = 0; t < 80; t++) {
            uint32_t f = (t < 20) ? ((b & c) | (~b & d)) : (t < 40) ? (b ^ c ^ d) : (t < 60) ? ((b & c) | (b & d) | (c & d)) : (b ^ c ^ d);
            uint32_t k = (t < 20) ? 0x5A827999 : (t < 40) ? 0x6ED9EBA1 : (t < 60) ? 0x8F1BBCDC : 0xCA62C1D6;
            uint32_t temp = _rotl(a, 5) + f + e + k + w[t];
            e = d; d = c; c = _rotl(b, 30); b = a; a = temp;
        }
        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }
    delete[] msg;
    output[0] = (uint8_t)(h0 >> 24); output[1] = (uint8_t)(h0 >> 16);
    output[2] = (uint8_t)(h0 >> 8);  output[3] = (uint8_t)(h0);
    output[4] = (uint8_t)(h1 >> 24); output[5] = (uint8_t)(h1 >> 16);
    output[6] = (uint8_t)(h1 >> 8);  output[7] = (uint8_t)(h1);
    output[8] = (uint8_t)(h2 >> 24); output[9] = (uint8_t)(h2 >> 16);
    output[10] = (uint8_t)(h2 >> 8); output[11] = (uint8_t)(h2);
    output[12] = (uint8_t)(h3 >> 24);output[13] = (uint8_t)(h3 >> 16);
    output[14] = (uint8_t)(h3 >> 8); output[15] = (uint8_t)(h3);
    output[16] = (uint8_t)(h4 >> 24);output[17] = (uint8_t)(h4 >> 16);
    output[18] = (uint8_t)(h4 >> 8); output[19] = (uint8_t)(h4);
}

static std::string base64_encode(const uint8_t* data, size_t len) {
    static const char* b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t triple = (uint32_t)data[i] << 16;
        if (i + 1 < len) triple |= (uint32_t)data[i + 1] << 8;
        if (i + 2 < len) triple |= data[i + 2];
        out += b64[(triple >> 18) & 0x3F];
        out += b64[(triple >> 12) & 0x3F];
        out += (i + 1 < len) ? b64[(triple >> 6) & 0x3F] : '=';
        out += (i + 2 < len) ? b64[triple & 0x3F] : '=';
    }
    return out;
}

// Unmask WebSocket frame payload
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
let reconnectTimer=null;
function connect(){
if(reconnectTimer)clearTimeout(reconnectTimer);
console.log('WebSocket connecting...');
try{
ws=new WebSocket('ws://'+location.host+'/ws');
}catch(e){console.error('WS create error:',e);reconnectTimer=setTimeout(connect,2000);return;}
ws.onopen=()=>{connected=true;document.getElementById('status').textContent='Connected';document.getElementById('status').className='status connected';console.log('WebSocket connected');};
ws.onclose=()=>{connected=false;document.getElementById('status').textContent='Disconnected';document.getElementById('status').className='status disconnected';reconnectTimer=setTimeout(connect,2000);console.log('WebSocket disconnected');};
ws.onmessage=(e)=>{try{update(JSON.parse(e.data));}catch(ex){console.error('parse error:',ex);}};
ws.onerror=(e)=>{console.error('WebSocket error:',e);ws.close();};
}
setTimeout(connect,500);

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
if(data.FPS!==undefined)document.getElementById('fps').textContent=data.FPS;
const canvas=document.getElementById('radar');
const ctx=canvas.getContext('2d');
ctx.clearRect(0,0,400,400);
ctx.fillStyle='#0c0c14';
ctx.fillRect(0,0,400,400);
if(data.InGame&&data.players.length>0){
const cx=200,cy=200,scale=1.5;
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
ctx.fillText((p.name||'?')+' '+Math.round(p.distance||0)+'m',px+6,py+3);
}
}
let h='';
for(const p of data.players){
h+='<div style="padding:4px;border-bottom:1px solid #222;font-size:12px">';
h+='<span style="color:'+(p.is_enemy?'#ff4444':'#44ff44')+'">●</span> ';
h+='<span>'+json_escape(p.name||'?')+'</span> ';
h+='<span style="color:#888;float:right">HP:'+(p.health||0)+' '+Math.round(p.distance||0)+'m</span>';
h+='</div>';
}
document.getElementById('players').innerHTML=h;
}
</script>
</body>
</html>
)HTML";

namespace sky::web {

// Thread-per-connection handler
static void handle_client(SOCKET client, WebServer* server) {
    // Read the HTTP request
    char buf[8192] = {};
    int received = recv(client, buf, sizeof(buf) - 1, 0);
    if (received <= 0) { closesocket(client); return; }

    std::string request(buf, received);

    // Parse path
    std::string path = "/";
    size_t gp = request.find("GET ");
    if (gp != std::string::npos) {
        gp += 4;
        size_t ge = request.find(" ", gp);
        if (ge != std::string::npos) path = request.substr(gp, ge - gp);
    }

    LOG_INFO("Request: " + path);

    if (path == "/ws") {
        // ----- WebSocket upgrade -----
        // Find Sec-WebSocket-Key (case-insensitive)
        std::string low_req;
        for (char c : request) low_req += (char)tolower((unsigned char)c);
        size_t kp = low_req.find("sec-websocket-key: ");
        if (kp == std::string::npos) { closesocket(client); return; }
        kp += 19;
        size_t ke = request.find("\r\n", kp);
        if (ke == std::string::npos) { closesocket(client); return; }
        std::string ws_key = request.substr(kp, ke - kp);

        std::string accept_key = ws_key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        uint8_t sha1[20];
        sha1_hash((const uint8_t*)accept_key.data(), accept_key.size(), sha1);
        std::string b64_key = base64_encode(sha1, 20);

        std::string response =
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: " + b64_key + "\r\n"
            "\r\n";
        send(client, response.c_str(), (int)response.size(), 0);
        LOG_INFO("WebSocket handshake sent");

        // Register client
        {
            std::lock_guard<std::mutex> lk(server->client_mutex);
            server->ws_clients.push_back(client);
        }
        LOG_INFO("WebSocket client connected (total: " + std::to_string(server->ws_clients.size()) + ")");

        // WebSocket frame loop
        while (server->running) {
            char rbuf[2048] = {};
            int r = recv(client, rbuf, sizeof(rbuf), 0);
            if (r <= 0) break;
            std::string data(rbuf, r);
            std::string msg = ws_unframe(data);
            if (msg.empty()) continue;

            // Parse commands
            if (msg.find("\"type\":\"cmd\"") != std::string::npos) {
                bool val = msg.find("\"value\":true") != std::string::npos;
                if (msg.find("\"key\":\"esp\"") != std::string::npos && server->on_esp_toggle) server->on_esp_toggle(val);
                else if (msg.find("\"key\":\"radar\"") != std::string::npos && server->on_radar_toggle) server->on_radar_toggle(val);
                else if (msg.find("\"key\":\"aimbot\"") != std::string::npos && server->on_aimbot_toggle) server->on_aimbot_toggle(val);
                else if (msg.find("\"key\":\"trigger\"") != std::string::npos && server->on_triggerbot_toggle) server->on_triggerbot_toggle(val);
            }
        }

        // Remove client
        {
            std::lock_guard<std::mutex> lk(server->client_mutex);
            auto& v = server->ws_clients;
            v.erase(std::remove(v.begin(), v.end(), client), v.end());
        }
        closesocket(client);
        LOG_INFO("WebSocket client disconnected");
        return;
    }

    // ----- HTTP request -----
    // Favicon: skip
    if (path == "/favicon.ico") {
        std::string r = "HTTP/1.1 204 No Content\r\n\r\n";
        send(client, r.c_str(), (int)r.size(), 0);
        closesocket(client);
        return;
    }

    // Serve HTML page
    std::string response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=UTF-8\r\n"
        "Connection: close\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n" +
        std::string(HTML_PAGE);
    send(client, response.c_str(), (int)response.size(), 0);
    closesocket(client);
}

bool WebServer::start(int port) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return false;

    m_listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_listen_socket == INVALID_SOCKET) { WSACleanup(); return false; }

    int opt = 1;
    setsockopt(m_listen_socket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(m_listen_socket, (sockaddr*)&addr, sizeof(addr)) != 0) {
        closesocket(m_listen_socket);
        WSACleanup();
        return false;
    }
    if (listen(m_listen_socket, 10) != 0) {
        closesocket(m_listen_socket);
        WSACleanup();
        return false;
    }

    LOG_INFO("Web server started on port " + std::to_string(port));
    running = true;
    m_accept_thread = std::thread([this]() {
        while (running) {
            SOCKET client = accept(m_listen_socket, nullptr, nullptr);
            if (client == INVALID_SOCKET) continue;
            // Spawn a thread for each connection
            std::thread(handle_client, client, this).detach();
        }
    });
    m_accept_thread.detach();
    return true;
}

void WebServer::stop() {
    running = false;
    if (m_listen_socket != INVALID_SOCKET) {
        closesocket(m_listen_socket);
        m_listen_socket = INVALID_SOCKET;
    }
    WSACleanup();
}

void WebServer::broadcast_ws(const std::string& msg) {
    // Build WebSocket frame
    std::string frame;
    frame += (char)0x81; // text frame, FIN
    size_t len = msg.size();
    if (len < 126) {
        frame += (char)len;
    } else if (len < 65536) {
        frame += (char)126;
        frame += (char)((len >> 8) & 0xFF);
        frame += (char)(len & 0xFF);
    } else {
        frame += (char)127;
        for (int i = 7; i >= 0; i--)
            frame += (char)((len >> (i * 8)) & 0xFF);
    }
    frame += msg;

    std::lock_guard<std::mutex> lk(client_mutex);
    for (auto it = ws_clients.begin(); it != ws_clients.end(); ) {
        SOCKET c = *it;
        int sent = send(c, frame.data(), (int)frame.size(), 0);
        if (sent <= 0) {
            closesocket(c);
            it = ws_clients.erase(it);
        } else {
            ++it;
        }
    }
}

void WebServer::update_snapshot(const GameSnapshot& snap) {
    std::lock_guard<std::mutex> snap_lock(snapshot_mutex);
    m_snapshot = snap;
    std::string json = "{";
    json += "\"InGame\":" + std::string(snap.InGame ? "true" : "false") + ",";
    json += "\"FPS\":0,";
    json += "\"camera_x\":" + std::to_string(snap.camera_x) + ",";
    json += "\"camera_y\":" + std::to_string(snap.camera_y) + ",";
    json += "\"camera_z\":" + std::to_string(snap.camera_z) + ",";
    json += "\"players\":[";
    for (size_t i = 0; i < snap.players.size(); i++) {
        if (i > 0) json += ",";
        json += "{";
        json += "\"name\":\"" + json_escape(snap.players[i].name) + "\",";
        json += "\"health\":" + std::to_string(snap.players[i].health) + ",";
        json += "\"distance\":" + std::to_string(snap.players[i].distance) + ",";
        json += "\"pos_x\":" + std::to_string(snap.players[i].pos_x) + ",";
        json += "\"pos_y\":" + std::to_string(snap.players[i].pos_y) + ",";
        json += "\"pos_z\":" + std::to_string(snap.players[i].pos_z) + ",";
        json += "\"is_enemy\":" + std::string(snap.players[i].is_enemy ? "true" : "false");
        json += "}";
    }
    json += "]}";
    broadcast_ws(json);
}

GameSnapshot WebServer::get_snapshot() const {
    std::lock_guard<std::mutex> lk(snapshot_mutex);
    return m_snapshot;
}

} // namespace sky::web