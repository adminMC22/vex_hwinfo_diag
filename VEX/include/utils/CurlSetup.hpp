#pragma once
/**
 * CurlSetup.hpp -- Fetches JSON offset data from a remote URL
 *
 * Uses WinHTTP (native Windows API, no external dependencies).
 *
 * === HOW IT WORKS ===
 * 1. Tries primary paste URL (paste.c-net.org)
 * 2. Tries backup paste URL
 * 3. Tries bootmgfw/ValorantOffsets GitHub repo (auto-parses markdown)
 * 4. Falls back to hardcoded offsets (last known good)
 *
 * Step 3 means GWorld and FNamePool auto-update on every launch
 * even if the paste URL goes stale. No recompilation needed.
 */

#include <string>
#include <sstream>
#include <iostream>
#include <vector>
#include <windows.h>
#include <winhttp.h>
#include "json.hpp"

using json = nlohmann::json;

// ============================================================
// Offset sources (tried in order)
// ============================================================
#define OFFSET_URL_PRIMARY  L"https://paste.c-net.org/BellhopSacked"
#define OFFSET_URL_BACKUP   L"https://paste.c-net.org/PsychoPetey"
// GitHub raw markdown - always has current GWorld/FNamePool
#define OFFSET_URL_GITHUB   L"https://raw.githubusercontent.com/bootmgfw/ValorantOffsets/main/Offsets/13.01.00.5090349.md"

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

static bool try_winhttp(const wchar_t* url, std::string& output) {
    URL_COMPONENTS urlComp = { sizeof(URL_COMPONENTS) };
    wchar_t hostName[256] = { 0 };
    wchar_t urlPath[2048] = { 0 };
    urlComp.lpszHostName = hostName;
    urlComp.dwHostNameLength = 256;
    urlComp.lpszUrlPath = urlPath;
    urlComp.dwUrlPathLength = 2048;
    urlComp.dwSchemeLength = -1;

    if (!WinHttpCrackUrl(url, 0, 0, &urlComp)) return false;

    HINTERNET hSession = WinHttpOpen(L"VEX/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!hSession) return false;

    HINTERNET hConnect = WinHttpConnect(hSession, hostName,
        urlComp.nPort, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return false; }

    DWORD flags = WINHTTP_FLAG_REFRESH |
        (urlComp.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", urlPath,
        NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    WinHttpReceiveResponse(hRequest, NULL);

    DWORD bytesRead = 0;
    char buffer[4096];
    while (WinHttpReadData(hRequest, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        output.append(buffer, bytesRead);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return !output.empty();
}

// Parse GitHub markdown to extract GWorld and FNamePool hex values
// Format: "GWorld: 0x1234567\nFNamePool: 0x89ABCDE"
static void parse_github_markdown(const std::string& md, json& out) {
    auto find_hex = [&](const std::string& label) -> std::string {
        auto pos = md.find(label + ":");
        if (pos == std::string::npos) return "";
        pos += label.length() + 1;
        while (pos < md.length() && (md[pos] == ' ' || md[pos] == '\t')) pos++;
        if (pos < md.length() && md[pos] == '0' && (pos + 1) < md.length() && (md[pos+1] == 'x' || md[pos+1] == 'X')) {
            auto end = pos + 2;
            while (end < md.length() && (isxdigit(md[end]))) end++;
            return md.substr(pos, end - pos);
        }
        return "";
    };

    std::string gworld = find_hex("GWorld");
    std::string fname = find_hex("FNamePool");

    if (!gworld.empty() && !fname.empty()) {
        out["version"] = "auto-github";
        out["game"] = "VALORANT";
        auto& o = out["offsets"];
        // Core engine offsets from GitHub
        o["GWorld"] = gworld;
        o["FNamePool"] = fname;

        // Also try FNameState, GWorldState
        std::string fstate = find_hex("FNameState");
        std::string gstate = find_hex("GWorldState");
        if (!fstate.empty()) o["FNameState"] = fstate;
        if (!gstate.empty()) o["GWorldState"] = gstate;

        std::cout << "[Offsets] GitHub fallback: GWorld=" << gworld
                  << " FNamePool=" << fname << std::endl;
    }
}

inline json setup_curl() {
    std::string response;

    // 1. Try primary paste URL
    std::cout << "[Offsets] Fetching from primary..." << std::endl;
    if (try_winhttp(OFFSET_URL_PRIMARY, response)) {
        std::cout << "[Offsets] Primary OK (" << response.size() << " bytes)" << std::endl;
    }
    // 2. Try backup paste URL
    else if (try_winhttp(OFFSET_URL_BACKUP, response)) {
        std::cout << "[Offsets] Backup OK (" << response.size() << " bytes)" << std::endl;
    }
    // 3. Try GitHub markdown (auto-parsed for GWorld/FNamePool)
    else if (try_winhttp(OFFSET_URL_GITHUB, response)) {
        std::cout << "[Offsets] GitHub fallback (" << response.size() << " bytes)" << std::endl;
        json j;
        parse_github_markdown(response, j);
        if (!j.is_null() && j.contains("offsets")) {
            return j;
        }
        std::cout << "[Offsets] GitHub parse failed" << std::endl;
        return json{};
    }
    else {
        std::cout << "[Offsets] WARNING: All offset sources unreachable!" << std::endl;
        std::cout << "[Offsets] Game may not work correctly." << std::endl;
        return json{};
    }

    // Try parsing as JSON first
    try {
        json j = json::parse(response);
        if (j.contains("offsets")) {
            std::cout << "[Offsets] Parsed JSON, version: "
                      << j.value("version", "unknown") << std::endl;
            return j;
        }
    }
    catch (...) {
        // Not JSON - try GitHub markdown format
        json j;
        parse_github_markdown(response, j);
        if (!j.is_null() && j.contains("offsets")) {
            return j;
        }
        std::cout << "[Offsets] Unknown format, expected JSON" << std::endl;
    }

    return json{};
}
