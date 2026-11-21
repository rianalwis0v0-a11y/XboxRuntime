#pragma once
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <cstdint>

// Original-Xbox XOnline ABI subset, based on RXDK-Libs shared/include/xonline.h.
// This is a guest32 compatibility surface, not a server wire-protocol header.
constexpr DWORD XONLINE_MAX_LOGON_USERS = 4;
constexpr DWORD XONLINE_MAX_STORED_ONLINE_USERS = 16;
constexpr DWORD XONLINE_GAMERTAG_SIZE = 16;
constexpr DWORD XONLINE_USER_RESERVED_SIZE = 72;

constexpr HRESULT XONLINE_E_OVERFLOW = static_cast<HRESULT>(0x80150001u);
constexpr HRESULT XONLINE_E_NOT_INITIALIZED = static_cast<HRESULT>(0x80150005u);
constexpr HRESULT XONLINE_E_NO_USER = static_cast<HRESULT>(0x80150006u);
constexpr HRESULT XONLINE_E_OUT_OF_MEMORY = static_cast<HRESULT>(0x80150008u);
constexpr HRESULT XONLINE_E_LOGON_CANNOT_ACCESS_SERVICE = static_cast<HRESULT>(0x80151001u);
constexpr HRESULT XONLINE_S_LOGON_CONNECTION_ESTABLISHED = static_cast<HRESULT>(0x001510F0u);
constexpr HRESULT XONLINETASK_S_SUCCESS = static_cast<HRESULT>(0x001500F0u);
constexpr HRESULT XONLINETASK_S_RESULTS_AVAIL = static_cast<HRESULT>(0x001500F1u);
constexpr HRESULT XONLINETASK_S_RUNNING_IDLE = static_cast<HRESULT>(0x001500F2u);
constexpr HRESULT XONLINETASK_S_RUNNING = S_OK;

struct XONLINE_STARTUP_PARAMS {
    DWORD dwMaxPrivatePool;
};

#pragma pack(push, 4)
struct XUID {
    union {
        ULONGLONG qwUserID;
        ULONGLONG qwTeamID;
    };
    DWORD dwUserFlags;
};

struct XONLINE_USER {
    XUID xuid;
    CHAR szGamertag[XONLINE_GAMERTAG_SIZE];
    DWORD dwUserOptions;
    BYTE passcode[4];
    BYTE reserved[XONLINE_USER_RESERVED_SIZE];
    HRESULT hr;
};

struct XONLINE_SERVICE_INFO {
    DWORD dwServiceID;
    IN_ADDR serviceIP;
    WORD wServicePort;
    WORD wReserved;
};
#pragma pack(pop)

struct XboxOnlineTask;
using XONLINETASK_HANDLE = XboxOnlineTask*;

static_assert(sizeof(XUID) == 12, "Original Xbox XUID ABI size");
static_assert(sizeof(XONLINE_USER) == 112, "Original Xbox XONLINE_USER ABI size");
static_assert(offsetof(XONLINE_USER, szGamertag) == 12, "XONLINE_USER gamertag offset");
static_assert(offsetof(XONLINE_USER, dwUserOptions) == 28, "XONLINE_USER options offset");
static_assert(offsetof(XONLINE_USER, passcode) == 32, "XONLINE_USER passcode offset");
static_assert(offsetof(XONLINE_USER, reserved) == 36, "XONLINE_USER reserved offset");
static_assert(offsetof(XONLINE_USER, hr) == 108, "XONLINE_USER HRESULT offset");
static_assert(sizeof(XONLINE_SERVICE_INFO) == 12, "XONLINE_SERVICE_INFO ABI size");

extern "C" {
HRESULT WINAPI XOnlineStartup(const XONLINE_STARTUP_PARAMS* params);
HRESULT WINAPI XOnlineCleanup();
HRESULT WINAPI XOnlineGetUsers(XONLINE_USER* users, DWORD* userCount);
HRESULT WINAPI XOnlineLogon(const XONLINE_USER* users, const DWORD* serviceIds,
    DWORD serviceCount, HANDLE workEvent, XONLINETASK_HANDLE* task);
HRESULT WINAPI XOnlineTaskContinue(XONLINETASK_HANDLE task);
HRESULT WINAPI XOnlineTaskClose(XONLINETASK_HANDLE task);
HRESULT WINAPI XOnlineLogonTaskGetResults(XONLINETASK_HANDLE task);
}
