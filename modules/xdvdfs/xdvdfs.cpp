#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <fstream>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include "xdvdfs.h"

namespace {
constexpr uint64_t kSector = 2048;
constexpr uint64_t kDescriptorOffset = 32 * kSector;
constexpr char kMagic[] = "MICROSOFT*XBOX*MEDIA";

struct Entry {
    uint32_t sector = 0;
    uint32_t size = 0;
    uint8_t attributes = 0;
    std::string name;
    bool directory = false;
};
struct Volume {
    std::ifstream file;
    uint64_t imageSize = 0;
    uint64_t partition = 0;
    uint32_t rootSector = 0;
    uint32_t rootSize = 0;
    uint64_t directoryBytesParsed = 0;
    std::unordered_map<std::string, Entry> entries;
    bool open = false;
};
Volume g_volume;
std::mutex g_mutex;
bool g_started = false;

uint16_t U16(const uint8_t* p) { return uint16_t(p[0] | (uint16_t(p[1]) << 8)); }
uint32_t U32(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }
bool RangeOK(uint64_t offset, uint64_t length, uint64_t total) {
    return offset <= total && length <= total - offset;
}
std::string Normalize(const char* path) {
    if (!path) return {};
    std::string out;
    while (*path == '/' || *path == '\\') ++path;
    for (; *path; ++path) {
        char c = *path == '\\' ? '/' : *path;
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
        out.push_back(c);
    }
    while (!out.empty() && out.back() == '/') out.pop_back();
    return out;
}
bool ReadAt(Volume& v, uint64_t offset, void* buffer, size_t bytes) {
    if (!RangeOK(offset, bytes, v.imageSize) || bytes > size_t(std::numeric_limits<std::streamsize>::max())) return false;
    v.file.clear();
    v.file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    return bool(v.file) && bool(v.file.read(static_cast<char*>(buffer), static_cast<std::streamsize>(bytes)));
}
bool ParseDescriptor(Volume& v, uint64_t partition) {
    uint8_t descriptor[kSector]{};
    if (!RangeOK(partition + kDescriptorOffset, kSector, v.imageSize) ||
        !ReadAt(v, partition + kDescriptorOffset, descriptor, sizeof(descriptor))) return false;
    if (std::memcmp(descriptor, kMagic, 20) != 0 ||
        std::memcmp(descriptor + 0x7EC, kMagic, 20) != 0) return false;
    const uint32_t rootSector = U32(descriptor + 0x14);
    const uint32_t rootSize = U32(descriptor + 0x18);
    if ((rootSector == 0) != (rootSize == 0)) return false;
    if (rootSize && !RangeOK(partition + uint64_t(rootSector) * kSector, rootSize, v.imageSize)) return false;
    v.partition = partition;
    v.rootSector = rootSector;
    v.rootSize = rootSize;
    return true;
}
bool ParseDirectory(Volume& v, const std::string& parent, uint32_t sector, uint32_t size, unsigned depth = 0) {
    if (!size) return true;
    if (depth >= 128) return false;
    if (size > 16u * 1024u * 1024u || v.directoryBytesParsed > 128u * 1024u * 1024u - size) return false;
    v.directoryBytesParsed += size;
    const uint64_t tableOffset = v.partition + uint64_t(sector) * kSector;
    if (!RangeOK(tableOffset, size, v.imageSize)) return false;
    std::vector<uint8_t> table(size);
    if (!ReadAt(v, tableOffset, table.data(), table.size())) return false;

    std::vector<uint32_t> stack{0};
    std::unordered_set<uint32_t> visited;
    while (!stack.empty()) {
        const uint32_t offset = stack.back();
        stack.pop_back();
        if (!visited.insert(offset).second) continue;
        if (visited.size() > 100000) return false;
        if (offset > size || size - offset < 14) return false;
        const uint8_t* p = table.data() + offset;
        const uint16_t left = U16(p);
        const uint16_t right = U16(p + 2);
        const uint32_t fileSector = U32(p + 4);
        const uint32_t fileSize = U32(p + 8);
        const uint8_t attributes = p[12];
        const uint8_t nameLength = p[13];
        if (!nameLength || nameLength > 255 || uint32_t(14 + nameLength) > size - offset) return false;
        const uint32_t entryBytes = (14u + nameLength + 3u) & ~3u;
        if (entryBytes > size - offset) return false;
        std::string name(reinterpret_cast<const char*>(p + 14), nameLength);
        if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos ||
            name == "." || name == "..") return false;
        const std::string key = parent.empty() ? Normalize(name.c_str()) : parent + "/" + Normalize(name.c_str());
        Entry entry{fileSector, fileSize, attributes, name, (attributes & 0x10) != 0};
        if (v.entries.size() >= 500000 || !v.entries.emplace(key, entry).second) return false;
        if (entry.directory) {
            if (fileSize && !ParseDirectory(v, key, fileSector, fileSize, depth + 1)) return false;
        } else if (fileSize) {
            const uint64_t dataOffset = v.partition + uint64_t(fileSector) * kSector;
            if (!RangeOK(dataOffset, fileSize, v.imageSize)) return false;
        }
        // Both encodings have appeared in published descriptions: zero and
        // 0xffff mean no child. Treat either as absent, with cycle/bounds checks.
        if (right != 0 && right != 0xFFFF) stack.push_back(uint32_t(right) * 4u);
        if (left != 0 && left != 0xFFFF) stack.push_back(uint32_t(left) * 4u);
    }
    return true;
}
void CloseUnlocked() {
    g_volume.file.close();
    g_volume.entries.clear();
    g_volume.open = false;
    g_volume.imageSize = g_volume.partition = 0;
    g_volume.rootSector = g_volume.rootSize = 0;
}
}

extern "C" int __cdecl ModuleStart(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_started = true;
    OutputDebugStringW(L"[XboxRuntime:XDVDFS] Read-only disc filesystem ready.\n");
    return 0;
}
extern "C" void __cdecl ModuleStop(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    CloseUnlocked();
    g_started = false;
}
extern "C" void __cdecl XDVDFS_CloseImage(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    CloseUnlocked();
}
extern "C" int __cdecl XDVDFS_OpenImage(const wchar_t* imagePath, XDVDFS_VOLUME_INFO* info) {
    if (!imagePath || !*imagePath || !info) return 1;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_started) return 2;
    CloseUnlocked();
    g_volume.file.open(imagePath, std::ios::binary);
    if (!g_volume.file) return 3;
    g_volume.file.seekg(0, std::ios::end);
    const auto end = g_volume.file.tellg();
    if (end < 0) { CloseUnlocked(); return 4; }
    g_volume.imageSize = static_cast<uint64_t>(end);

    // Raw XISO and original Xbox XGD1 game-partition layouts.
    const uint64_t candidates[] = { 0, 0x18300000ull };
    bool found = false;
    for (uint64_t base : candidates) {
        if (ParseDescriptor(g_volume, base)) { found = true; break; }
    }
    if (!found) { CloseUnlocked(); return 5; }
    if (g_volume.rootSize && !ParseDirectory(g_volume, "", g_volume.rootSector, g_volume.rootSize)) {
        CloseUnlocked(); return 6;
    }
    g_volume.open = true;
    info->partitionOffset = g_volume.partition;
    info->rootDirectorySector = g_volume.rootSector;
    info->rootDirectorySize = g_volume.rootSize;
    info->imageSize = g_volume.imageSize;
    return 0;
}
extern "C" int __cdecl XDVDFS_GetFileInfo(const char* path, XDVDFS_FILE_INFO* info) {
    if (!path || !info) return 1;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_started || !g_volume.open) return 2;
    const auto it = g_volume.entries.find(Normalize(path));
    if (it == g_volume.entries.end()) return 3;
    const Entry& e = it->second;
    std::memset(info, 0, sizeof(*info));
    info->startSector = e.sector;
    info->size = e.size;
    info->attributes = e.attributes;
    info->nameLength = static_cast<uint8_t>((std::min)(e.name.size(), size_t(255)));
    std::memcpy(info->name, e.name.data(), info->nameLength);
    return 0;
}
extern "C" int __cdecl XDVDFS_ListDirectory(
    const char* path, XDVDFS_FILE_INFO* output,
    uint32_t capacity, uint32_t* required) {
    if (!path || !required || (capacity && !output)) return 1;
    *required = 0;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_started || !g_volume.open) return 2;

    const std::string directory = Normalize(path);
    if (!directory.empty()) {
        const auto dir = g_volume.entries.find(directory);
        if (dir == g_volume.entries.end()) return 3;
        if (!dir->second.directory) return 4;
    }

    const std::string prefix = directory.empty() ? std::string() : directory + "/";
    std::vector<std::pair<std::string, const Entry*>> children;
    for (const auto& item : g_volume.entries) {
        const std::string& key = item.first;
        if (key.size() <= prefix.size() ||
            key.compare(0, prefix.size(), prefix) != 0)
            continue;
        const std::string remainder = key.substr(prefix.size());
        if (remainder.find('/') != std::string::npos) continue;
        children.emplace_back(remainder, &item.second);
    }

    std::sort(children.begin(), children.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });
    *required = static_cast<uint32_t>(children.size());
    if (capacity < children.size()) return XDVDFS_ERROR_BUFFER_TOO_SMALL;

    for (uint32_t i = 0; i < *required; ++i) {
        const Entry& source = *children[i].second;
        XDVDFS_FILE_INFO& target = output[i];
        std::memset(&target, 0, sizeof(target));
        target.startSector = source.sector;
        target.size = source.size;
        target.attributes = source.attributes;
        target.nameLength = static_cast<uint8_t>(
            (std::min)(source.name.size(), size_t(255)));
        std::memcpy(target.name, source.name.data(), target.nameLength);
    }
    return 0;
}

extern "C" int __cdecl XDVDFS_ReadFile(const char* path, uint64_t offset, void* buffer, uint32_t bytesToRead, uint32_t* bytesRead) {
    if (bytesRead) *bytesRead = 0;
    if (!path || (!buffer && bytesToRead) || !bytesRead) return 1;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_started || !g_volume.open) return 2;
    const auto it = g_volume.entries.find(Normalize(path));
    if (it == g_volume.entries.end()) return 3;
    const Entry& e = it->second;
    if (e.directory) return 4;
    if (offset > e.size) return 5;
    const uint32_t count = static_cast<uint32_t>((std::min)(uint64_t(bytesToRead), uint64_t(e.size) - offset));
    if (!count) return 0;
    const uint64_t dataOffset = g_volume.partition + uint64_t(e.sector) * kSector + offset;
    if (!ReadAt(g_volume, dataOffset, buffer, count)) return 6;
    *bytesRead = count;
    return 0;
}
BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }
