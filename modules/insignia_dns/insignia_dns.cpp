#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <winhttp.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdint>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

#include "insignia_dns.h"

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Winhttp.lib")

namespace {
constexpr wchar_t kZonesHost[] = L"insignia.live";
constexpr wchar_t kZonesPath[] = L"/dns_zones.json";
constexpr unsigned short kDnsPort = 53;
constexpr DWORD kRefreshMilliseconds = 5 * 60 * 1000;
constexpr size_t kPacketLimit = 4096;

struct Zone {
    std::string name;
    uint32_t address; // network byte order
};

SRWLOCK g_zoneLock = SRWLOCK_INIT;
std::unordered_map<std::string, uint32_t> g_zones;
SOCKET g_socket = INVALID_SOCKET;
SOCKET g_tcpListener = INVALID_SOCKET;
HANDLE g_dnsThread = nullptr;
HANDLE g_tcpThread = nullptr;
HANDLE g_refreshThread = nullptr;
HANDLE g_stopEvent = nullptr;
volatile LONG g_running = 0;
bool g_winsock = false;

void Log(const wchar_t* message) {
    OutputDebugStringW(L"[XboxRuntime:InsigniaDNS] ");
    OutputDebugStringW(message);
    OutputDebugStringW(L"\n");
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool JsonString(const std::string& object, const char* key, std::string& value) {
    const std::regex pattern(std::string("\"") + key + "\"\\s*:\\s*\"([^\"]*)\"");
    std::smatch match;
    if (!std::regex_search(object, match, pattern)) return false;
    value = match[1].str();
    return true;
}

bool ResolveIPv4(const std::string& host, uint32_t& address) {
    ADDRINFOA hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    PADDRINFOA result = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0 || !result)
        return false;
    address = reinterpret_cast<sockaddr_in*>(result->ai_addr)->sin_addr.s_addr;
    freeaddrinfo(result);
    return true;
}

bool FetchZones() {
    HINTERNET session = WinHttpOpen(
        L"XboxRuntime-InsigniaDNS/1.0",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;

    HINTERNET connection = WinHttpConnect(session, kZonesHost,
        INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET request = connection ? WinHttpOpenRequest(connection, L"GET",
        kZonesPath, nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;

    bool ok = false;
    std::string body;
    if (request && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr)) {
        DWORD status = 0, statusSize = sizeof(status);
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                WINHTTP_NO_HEADER_INDEX) && status == 200) {
            for (;;) {
                DWORD available = 0;
                if (!WinHttpQueryDataAvailable(request, &available) || !available) break;
                std::vector<char> chunk(available);
                DWORD read = 0;
                if (!WinHttpReadData(request, chunk.data(), available, &read)) break;
                body.append(chunk.data(), read);
            }
            ok = !body.empty();
        }
    }

    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    if (!ok) return false;

    std::unordered_map<std::string, uint32_t> fresh;
    const std::regex objectPattern("\\{[^{}]*\\}");
    for (auto it = std::sregex_iterator(body.begin(), body.end(), objectPattern);
         it != std::sregex_iterator(); ++it) {
        const std::string object = it->str();
        std::string type, name, value;
        if (!JsonString(object, "type", type) ||
            !JsonString(object, "name", name) ||
            !JsonString(object, "value", value)) continue;
        if (type != "a" && type != "p") continue;
        uint32_t ip = 0;
        if (type == "a") {
            IN_ADDR parsed{};
            if (InetPtonA(AF_INET, value.c_str(), &parsed) != 1) continue;
            ip = parsed.s_addr;
        } else if (!ResolveIPv4(value, ip)) {
            continue;
        }
        name = Lower(name);
        if (!name.empty() && name.back() == '.') name.pop_back();
        fresh[name] = ip;
    }

    if (fresh.empty()) return false;
    AcquireSRWLockExclusive(&g_zoneLock);
    g_zones.swap(fresh);
    ReleaseSRWLockExclusive(&g_zoneLock);
    Log(L"Insignia DNS zone data refreshed.");
    return true;
}

uint16_t Read16(const uint8_t* p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}
void Write16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v >> 8);
    p[1] = static_cast<uint8_t>(v);
}
void Write32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >> 8);
    p[3] = static_cast<uint8_t>(v);
}

bool ReadQName(const uint8_t* packet, size_t length, size_t& pos, std::string& name) {
    name.clear();
    while (pos < length) {
        const uint8_t labelLength = packet[pos++];
        if ((labelLength & 0xC0) != 0 || labelLength > 63) return false;
        if (labelLength == 0) return true;
        if (pos + labelLength > length) return false;
        if (!name.empty()) name.push_back('.');
        name.append(reinterpret_cast<const char*>(packet + pos), labelLength);
        pos += labelLength;
        if (name.size() > 253) return false;
    }
    return false;
}

int MakeReply(const uint8_t* query, size_t queryLength, uint8_t* reply, size_t capacity) {
    if (queryLength < 17 || capacity < 32 || Read16(query + 4) != 1) return 0;
    size_t questionEnd = 12;
    std::string name;
    if (!ReadQName(query, queryLength, questionEnd, name) || questionEnd + 4 > queryLength)
        return 0;

    const uint16_t qtype = Read16(query + questionEnd);
    const uint16_t qclass = Read16(query + questionEnd + 2);
    questionEnd += 4;
    if (questionEnd > capacity) return 0;

    std::copy(query, query + 12, reply);
    std::copy(query + 12, query + questionEnd, reply + 12);
    const uint16_t requestFlags = Read16(query + 2);
    Write16(reply + 2, static_cast<uint16_t>(0x8400 | (requestFlags & 0x0100)));
    Write16(reply + 4, 1);
    Write16(reply + 6, 0);
    Write16(reply + 8, 0);
    Write16(reply + 10, 0);

    uint32_t address = 0;
    bool found = false;
    if (qclass == 1 && (qtype == 1 || qtype == 255)) {
        AcquireSRWLockShared(&g_zoneLock);
        auto it = g_zones.find(Lower(name));
        if (it != g_zones.end()) { address = it->second; found = true; }
        ReleaseSRWLockShared(&g_zoneLock);
    }

    size_t out = questionEnd;
    if (found && out + 16 <= capacity) {
        reply[out++] = 0xC0; reply[out++] = 0x0C; // compressed owner name
        Write16(reply + out, 1); out += 2;       // A
        Write16(reply + out, 1); out += 2;       // IN
        Write32(reply + out, 300); out += 4;
        Write16(reply + out, 4); out += 2;
        std::memcpy(reply + out, &address, 4); out += 4;
        Write16(reply + 6, 1);
    }
    return static_cast<int>(out);
}

DWORD WINAPI DnsWorker(void*) {
    uint8_t query[kPacketLimit], reply[kPacketLimit];
    while (InterlockedCompareExchange(&g_running, 1, 1)) {
        sockaddr_in client{};
        int clientLength = sizeof(client);
        const int received = recvfrom(g_socket, reinterpret_cast<char*>(query),
            static_cast<int>(sizeof(query)), 0,
            reinterpret_cast<sockaddr*>(&client), &clientLength);
        if (received == SOCKET_ERROR) {
            if (!InterlockedCompareExchange(&g_running, 1, 1)) break;
            continue;
        }
        const int replyLength = MakeReply(query, static_cast<size_t>(received),
            reply, sizeof(reply));
        if (replyLength > 0) {
            sendto(g_socket, reinterpret_cast<const char*>(reply), replyLength, 0,
                reinterpret_cast<sockaddr*>(&client), clientLength);
        }
    }
    return 0;
}

bool RecvExact(SOCKET s, char* data, int length) {
    int done = 0;
    while (done < length) {
        int n = recv(s, data + done, length - done, 0);
        if (n <= 0) return false;
        done += n;
    }
    return true;
}

bool SendExact(SOCKET s, const char* data, int length) {
    int done = 0;
    while (done < length) {
        int n = send(s, data + done, length - done, 0);
        if (n <= 0) return false;
        done += n;
    }
    return true;
}

DWORD WINAPI TcpWorker(void*) {
    uint8_t query[kPacketLimit], reply[kPacketLimit];
    while (InterlockedCompareExchange(&g_running, 1, 1)) {
        SOCKET client = accept(g_tcpListener, nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            if (!InterlockedCompareExchange(&g_running, 1, 1)) break;
            continue;
        }
        DWORD timeout = 3000;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
            reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        setsockopt(client, SOL_SOCKET, SO_SNDTIMEO,
            reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        uint8_t prefix[2]{};
        if (RecvExact(client, reinterpret_cast<char*>(prefix), 2)) {
            const size_t length = Read16(prefix);
            if (length <= sizeof(query) && length >= 17 &&
                RecvExact(client, reinterpret_cast<char*>(query), static_cast<int>(length))) {
                const int replyLength = MakeReply(query, length, reply, sizeof(reply));
                if (replyLength > 0) {
                    Write16(prefix, static_cast<uint16_t>(replyLength));
                    SendExact(client, reinterpret_cast<const char*>(prefix), 2);
                    SendExact(client, reinterpret_cast<const char*>(reply), replyLength);
                }
            }
        }
        closesocket(client);
    }
    return 0;
}

DWORD WINAPI RefreshWorker(void*) {
    while (WaitForSingleObject(g_stopEvent, kRefreshMilliseconds) == WAIT_TIMEOUT) {
        if (InterlockedCompareExchange(&g_running, 1, 1)) FetchZones();
    }
    return 0;
}
} // namespace

extern "C" int __cdecl ModuleStart(void) {
    if (InterlockedCompareExchange(&g_running, 1, 1)) return 0;
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        Log(L"Winsock initialization failed.");
        return 1;
    }
    g_winsock = true;
    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_stopEvent) { WSACleanup(); g_winsock = false; return 2; }

    g_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_socket == INVALID_SOCKET) {
        Log(L"Could not create DNS socket.");
        CloseHandle(g_stopEvent); g_stopEvent = nullptr;
        WSACleanup(); g_winsock = false; return 2;
    }

    DWORD timeout = 1000;
    setsockopt(g_socket, SOL_SOCKET, SO_RCVTIMEO,
        reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(kDnsPort);
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(g_socket, reinterpret_cast<sockaddr*>(&local), sizeof(local)) == SOCKET_ERROR) {
        Log(L"Could not bind UDP port 53. Check for another DNS server or permissions.");
        closesocket(g_socket); g_socket = INVALID_SOCKET;
        // DNS is optional for offline use; don't prevent the dashboard from booting.
        CloseHandle(g_stopEvent); g_stopEvent = nullptr;
        WSACleanup(); g_winsock = false; return 0;
    }

    g_tcpListener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_tcpListener == INVALID_SOCKET ||
        bind(g_tcpListener, reinterpret_cast<sockaddr*>(&local), sizeof(local)) == SOCKET_ERROR ||
        listen(g_tcpListener, SOMAXCONN) == SOCKET_ERROR) {
        Log(L"Could not bind TCP port 53; built-in DNS will remain disabled.");
        if (g_tcpListener != INVALID_SOCKET) closesocket(g_tcpListener);
        g_tcpListener = INVALID_SOCKET;
        closesocket(g_socket); g_socket = INVALID_SOCKET;
        CloseHandle(g_stopEvent); g_stopEvent = nullptr;
        WSACleanup(); g_winsock = false; return 0;
    }

    InterlockedExchange(&g_running, 1);
    FetchZones(); // A failed first fetch is non-fatal; the refresh worker retries.
    g_dnsThread = CreateThread(nullptr, 0, DnsWorker, nullptr, 0, nullptr);
    g_tcpThread = CreateThread(nullptr, 0, TcpWorker, nullptr, 0, nullptr);
    g_refreshThread = CreateThread(nullptr, 0, RefreshWorker, nullptr, 0, nullptr);
    if (!g_dnsThread || !g_tcpThread || !g_refreshThread) {
        InterlockedExchange(&g_running, 0);
        SetEvent(g_stopEvent);
        closesocket(g_socket); g_socket = INVALID_SOCKET;
        closesocket(g_tcpListener); g_tcpListener = INVALID_SOCKET;
        if (g_dnsThread) { WaitForSingleObject(g_dnsThread, 2000); CloseHandle(g_dnsThread); g_dnsThread = nullptr; }
        if (g_tcpThread) { WaitForSingleObject(g_tcpThread, 2000); CloseHandle(g_tcpThread); g_tcpThread = nullptr; }
        if (g_refreshThread) { WaitForSingleObject(g_refreshThread, 3000); CloseHandle(g_refreshThread); g_refreshThread = nullptr; }
        CloseHandle(g_stopEvent); g_stopEvent = nullptr;
        WSACleanup(); g_winsock = false; return 4;
    }
    Log(L"Insignia DNS listening on UDP and TCP port 53 (all network interfaces).");
    return 0;
}

extern "C" void __cdecl ModuleStop(void) {
    if (!InterlockedExchange(&g_running, 0)) return;
    if (g_stopEvent) SetEvent(g_stopEvent);
    if (g_socket != INVALID_SOCKET) { closesocket(g_socket); g_socket = INVALID_SOCKET; }
    if (g_tcpListener != INVALID_SOCKET) { closesocket(g_tcpListener); g_tcpListener = INVALID_SOCKET; }
    if (g_dnsThread) { WaitForSingleObject(g_dnsThread, 2000); CloseHandle(g_dnsThread); g_dnsThread = nullptr; }
    if (g_tcpThread) { WaitForSingleObject(g_tcpThread, 2000); CloseHandle(g_tcpThread); g_tcpThread = nullptr; }
    if (g_refreshThread) { WaitForSingleObject(g_refreshThread, 3000); CloseHandle(g_refreshThread); g_refreshThread = nullptr; }
    if (g_stopEvent) { CloseHandle(g_stopEvent); g_stopEvent = nullptr; }
    if (g_winsock) { WSACleanup(); g_winsock = false; }
    Log(L"Insignia DNS stopped.");
}

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }
