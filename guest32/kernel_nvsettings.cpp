#include "kernel_nvsettings.h"

#include <array>
#include <cstring>
#include <mutex>
#include <string>

namespace {
constexpr std::uint32_t kStatusSuccess = 0x00000000u;
constexpr std::uint32_t kStatusInvalidParameter = 0xC000000Du;
constexpr std::uint32_t kStatusDeviceNotConnected = 0xC000009Du;
constexpr std::uint32_t kStatusBufferTooSmall = 0xC0000023u;
constexpr std::uint32_t kStatusNotSupported = 0xC00000BBu;
constexpr std::uint32_t kRegBinary = 3;
constexpr std::uint32_t kRegDword = 4;
constexpr std::uint32_t kEepromBytes = 256;
constexpr std::uint32_t kUserChecksumOffset = 0x60;
constexpr std::uint32_t kUserDataOffset = 0x64;
constexpr std::uint32_t kUserDataBytes = 0x5C;

std::array<std::uint8_t, kEepromBytes> g_eeprom{};
std::wstring g_path;
std::mutex g_mutex;
bool g_ready = false;

struct SettingRow {
    std::uint32_t index, offset, length, type;
    bool writable;
};

// Index order and type/length follow the pinned NXDK XC_* constants;
// byte offsets follow the documented 256-byte retail EEPROM layout.
// Unverified factory indices and encrypted fields are intentionally not mapped.
constexpr SettingRow kSettings[] = {
    {0x0000, 0x64, 4, kRegDword, false}, // XC_TIMEZONE_BIAS
    {0x0001, 0x68, 4, kRegBinary, true }, // XC_TZ_STD_NAME
    {0x0002, 0x78, 4, kRegDword, true }, // XC_TZ_STD_DATE
    {0x0003, 0x88, 4, kRegDword, false}, // XC_TZ_STD_BIAS
    {0x0004, 0x6C, 4, kRegBinary, false}, // XC_TZ_DLT_NAME
    {0x0005, 0x7C, 4, kRegDword, false}, // XC_TZ_DLT_DATE
    {0x0006, 0x8C, 4, kRegDword, false}, // XC_TZ_DLT_BIAS
    {0x0007, 0x90, 4, kRegDword, true }, // XC_LANGUAGE
    {0x0008, 0x94, 4, kRegDword, true }, // XC_VIDEO
    {0x0009, 0x98, 4, kRegDword, true }, // XC_AUDIO
    {0x000A, 0x9C, 4, kRegDword, true }, // XC_P_CONTROL_GAMES
    {0x000B, 0xA0, 4, kRegDword, true }, // XC_P_CONTROL_PASSWORD
    {0x000C, 0xA4, 4, kRegDword, true }, // XC_P_CONTROL_MOVIES
    {0x000D, 0xA8, 4, kRegBinary, false}, // XC_ONLINE_IP_ADDRESS
    {0x000E, 0xAC, 4, kRegBinary, false}, // XC_ONLINE_DNS_ADDRESS
    {0x000F, 0xB0, 4, kRegBinary, false}, // XC_ONLINE_DEFAULT_GATEWAY_ADDRESS
    {0x0010, 0xB4, 4, kRegBinary, false}, // XC_ONLINE_SUBNET_ADDRESS
    {0x0011, 0xB8, 4, kRegDword, true }, // XC_MISC
    {0x0012, 0xBC, 4, kRegDword, false}, // XC_DVD_REGION
    {0x00FF, 0x60, 0x60, kRegBinary, false}, // XC_MAX_OS: complete user-settings section
    {0x01FF, 0x30, 0x30, kRegBinary, false}, // XC_MAX_FACTORY: clear factory section
    {0xFFFE, 0x00, 0x30, kRegBinary, false}, // XC_ENCRYPTED_SECTION: raw encrypted factory section
};

bool PersistLocked() {
    if (!g_ready || g_path.empty()) return false;
    HANDLE file = CreateFileW(g_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = WriteFile(file, g_eeprom.data(), static_cast<DWORD>(g_eeprom.size()), &written, nullptr);
    if (ok) ok = written == g_eeprom.size() && FlushFileBuffers(file);
    CloseHandle(file);
    return ok != FALSE;
}
}

std::uint32_t XboxNvComputeChecksum(const std::uint8_t* bytes, std::size_t length) {
    if (!bytes || (length & 3u)) return 0;
    std::uint32_t checksum = 0;
    for (std::size_t offset = 0; offset + 4 <= length; offset += 4) {
        std::uint32_t word = 0;
        std::memcpy(&word, bytes + offset, sizeof(word));
        const std::uint64_t sum = std::uint64_t(checksum) + word;
        checksum = static_cast<std::uint32_t>(sum);
        if (sum > 0xFFFFFFFFull) ++checksum;
    }
    return ~checksum;
}

bool XboxNvGetSettingInfo(std::uint32_t index, XboxNvSettingInfo* info) {
    if (!info) return false;
    if (index == 0xFFFFu) {
        *info = {0, kEepromBytes, kRegBinary, false}; // XC_MAX_ALL
        return true;
    }
    for (const auto& row : kSettings) {
        if (row.index == index) {
            *info = {row.offset, row.length, row.type, row.writable};
            return true;
        }
    }
    return false;
}

bool XboxNvInitialize(const wchar_t* path) {
    if (!path || !*path) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_path = path;
    HANDLE file = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(file, &size) != FALSE;
    if (ok && size.QuadPart == 0) {
        // Keep the existing runtime policy: a blank image is explicitly unprovisioned.
        g_eeprom.fill(0xFF);
        DWORD written = 0;
        ok = WriteFile(file, g_eeprom.data(), static_cast<DWORD>(g_eeprom.size()), &written, nullptr) &&
             written == g_eeprom.size() && FlushFileBuffers(file);
    } else if (ok && size.QuadPart == static_cast<LONGLONG>(g_eeprom.size())) {
        DWORD read = 0;
        ok = SetFilePointer(file, 0, nullptr, FILE_BEGIN) != INVALID_SET_FILE_POINTER &&
             ReadFile(file, g_eeprom.data(), static_cast<DWORD>(g_eeprom.size()), &read, nullptr) &&
             read == g_eeprom.size();
    } else {
        ok = false; // Never truncate or replace a user's EEPROM backup.
    }
    CloseHandle(file);
    g_ready = ok;
    return ok;
}

void XboxNvFlush() {
    std::lock_guard<std::mutex> lock(g_mutex);
    PersistLocked();
}

bool XboxNvReadRaw(std::uint32_t offset, void* buffer, std::uint32_t length) {
    if (!buffer || offset > kEepromBytes || length > kEepromBytes - offset) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_ready) return false;
    std::memcpy(buffer, g_eeprom.data() + offset, length);
    return true;
}

bool XboxNvWriteRaw(std::uint32_t offset, const void* buffer, std::uint32_t length) {
    if (!buffer || offset > kEepromBytes || length > kEepromBytes - offset) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_ready) return false;
    std::array<std::uint8_t, kEepromBytes> previous{};
    std::memcpy(previous.data(), g_eeprom.data() + offset, length);
    std::memcpy(g_eeprom.data() + offset, buffer, length);
    if (PersistLocked()) return true;
    std::memcpy(g_eeprom.data() + offset, previous.data(), length);
    PersistLocked(); // Best-effort rollback if the first disk write was partial.
    return false;
}

extern "C" XboxNvStatus __stdcall K_ExQueryNonVolatileSetting(
    std::uint32_t valueIndex, std::uint32_t* type, void* value,
    std::uint32_t valueLength, std::uint32_t* resultLength) {
    if (!value) return static_cast<XboxNvStatus>(kStatusInvalidParameter);
    XboxNvSettingInfo info{};
    if (!XboxNvGetSettingInfo(valueIndex, &info)) {
        if (valueIndex >= 0x0100u && valueIndex <= 0x0104u)
            return static_cast<XboxNvStatus>(kStatusNotSupported);
        return static_cast<XboxNvStatus>(kStatusInvalidParameter);
    }
    if (resultLength) *resultLength = info.length;
    if (valueLength < info.length)
        return static_cast<XboxNvStatus>(kStatusBufferTooSmall);
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_ready) return static_cast<XboxNvStatus>(kStatusDeviceNotConnected);
    std::memcpy(value, g_eeprom.data() + info.offset, info.length);
    if (type) *type = info.type;
    return static_cast<XboxNvStatus>(kStatusSuccess);
}

extern "C" XboxNvStatus __stdcall K_ExSaveNonVolatileSetting(
    std::uint32_t valueIndex, std::uint32_t type, const void* value,
    std::uint32_t valueLength) {
    if (!value) return static_cast<XboxNvStatus>(kStatusInvalidParameter);
    XboxNvSettingInfo info{};
    if (!XboxNvGetSettingInfo(valueIndex, &info)) {
        if (valueIndex >= 0x0100u && valueIndex <= 0x0104u)
            return static_cast<XboxNvStatus>(kStatusNotSupported);
        return static_cast<XboxNvStatus>(kStatusInvalidParameter);
    }
    if (!info.writable)
        return static_cast<XboxNvStatus>(kStatusNotSupported);
    if (type != info.type || valueLength != info.length)
        return static_cast<XboxNvStatus>(kStatusInvalidParameter);

    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_ready) return static_cast<XboxNvStatus>(kStatusDeviceNotConnected);
    std::array<std::uint8_t, 4> previousValue{};
    std::uint32_t previousChecksum = 0;
    std::memcpy(previousValue.data(), g_eeprom.data() + info.offset, info.length);
    std::memcpy(&previousChecksum, g_eeprom.data() + kUserChecksumOffset, sizeof(previousChecksum));
    std::memcpy(g_eeprom.data() + info.offset, value, info.length);
    const std::uint32_t checksum = XboxNvComputeChecksum(g_eeprom.data() + kUserDataOffset, kUserDataBytes);
    std::memcpy(g_eeprom.data() + kUserChecksumOffset, &checksum, sizeof(checksum));
    if (PersistLocked()) return static_cast<XboxNvStatus>(kStatusSuccess);
    std::memcpy(g_eeprom.data() + info.offset, previousValue.data(), info.length);
    std::memcpy(g_eeprom.data() + kUserChecksumOffset, &previousChecksum, sizeof(previousChecksum));
    PersistLocked(); // Best-effort rollback if the first disk write was partial.
    return static_cast<XboxNvStatus>(kStatusDeviceNotConnected);
}

extern "C" XboxNvStatus __stdcall K_ExReadWriteRefurbInfo(
    XboxRefurbInfo32* refurbInfo, std::uint32_t valueLength, std::uint8_t doWrite) {
    (void)doWrite;
    if (!refurbInfo || valueLength < sizeof(XboxRefurbInfo32))
        return static_cast<XboxNvStatus>(kStatusInvalidParameter);
    return static_cast<XboxNvStatus>(kStatusNotSupported);
}

extern "C" std::uint32_t __stdcall K_AvSetDisplayMode(
    void* registerBase, std::uint32_t step, std::uint32_t displayMode,
    std::uint32_t sourceColorFormat, std::uint32_t pitch, std::uint32_t frameBuffer) {
    (void)registerBase; (void)step; (void)displayMode;
    (void)sourceColorFormat; (void)pitch; (void)frameBuffer;
    return kStatusNotSupported;
}
