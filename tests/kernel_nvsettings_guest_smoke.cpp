#include "kernel_nvsettings.h"

#include <cstdio>
#include <cstdint>
#include <cstring>

namespace {
using QueryFn = XboxNvStatus (__stdcall*)(std::uint32_t, std::uint32_t*, void*, std::uint32_t, std::uint32_t*);
using SaveFn = XboxNvStatus (__stdcall*)(std::uint32_t, std::uint32_t, const void*, std::uint32_t);

extern "C" __declspec(naked) void NvSettingsTestDispatch() {
    __asm {
        mov eax, [esp]
        add esp, 4
        cmp eax, 24
        je query_nv
        cmp eax, 29
        je save_nv
        ret
    query_nv:
        jmp K_ExQueryNonVolatileSetting
    save_nv:
        jmp K_ExSaveNonVolatileSetting
    }
}

void* MakeOrdinalThunk(std::uint32_t ordinal) {
    auto* code = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 10, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!code) return nullptr;
    code[0] = 0x68; // push imm32 (the production KernelDispatch thunk format)
    std::memcpy(code + 1, &ordinal, sizeof(ordinal));
    code[5] = 0xE9; // jmp rel32 to ordinal dispatcher
    const std::int32_t relative = static_cast<std::int32_t>(
        reinterpret_cast<std::uintptr_t>(&NvSettingsTestDispatch) -
        reinterpret_cast<std::uintptr_t>(code + 10));
    std::memcpy(code + 6, &relative, sizeof(relative));
    FlushInstructionCache(GetCurrentProcess(), code, 10);
    return code;
}

bool Check(bool ok, const char* message) {
    if (!ok) std::fprintf(stderr, "FAIL: %s\n", message);
    return ok;
}
}

int main() {
    wchar_t tempPath[MAX_PATH]{};
    wchar_t tempFile[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, tempPath) || !GetTempFileNameW(tempPath, L"xnv", 0, tempFile))
        return 2;
    DeleteFileW(tempFile);

    bool ok = Check(XboxNvInitialize(tempFile), "initialize blank EEPROM");
    void* queryCode = MakeOrdinalThunk(24);
    void* saveCode = MakeOrdinalThunk(29);
    auto query = reinterpret_cast<QueryFn>(queryCode);
    auto save = reinterpret_cast<SaveFn>(saveCode);
    ok &= Check(query != nullptr && save != nullptr, "allocate x86 ordinal import thunks");
    if (!ok) return 3;

    std::uint8_t raw[256]{};
    std::uint32_t type = 0, length = 0;
    ok &= Check(query(0xFFFFu, &type, raw, sizeof(raw), &length) == 0, "ordinal 24 queries full EEPROM");
    ok &= Check(type == 3 && length == sizeof(raw), "full EEPROM type and length");
    ok &= Check(raw[0] == 0xFF && raw[255] == 0xFF, "blank EEPROM remains unprovisioned");

    std::uint32_t language = 3;
    ok &= Check(save(7, 4, &language, sizeof(language)) == 0, "ordinal 29 saves writable language setting");
    std::uint32_t queried = 0;
    type = length = 0;
    ok &= Check(query(7, &type, &queried, sizeof(queried), &length) == 0, "ordinal 24 queries language");
    ok &= Check(queried == language && type == 4 && length == sizeof(language), "language value/type/length");
    ok &= Check(raw[0] == 0xFF, "raw EEPROM snapshot buffer was not aliased");

    std::uint8_t tooSmall[3] = {0xA5, 0xA5, 0xA5};
    length = 0;
    ok &= Check(query(7, &type, tooSmall, sizeof(tooSmall), &length) == static_cast<XboxNvStatus>(0xC0000023u),
                "short output buffer returns STATUS_BUFFER_TOO_SMALL");
    ok &= Check(length == 4 && tooSmall[0] == 0xA5, "short buffer reports required size without writing");

    ok &= Check(save(0, 4, &language, sizeof(language)) == static_cast<XboxNvStatus>(0xC00000BBu),
                "read-only timezone bias save is explicitly unsupported");
    ok &= Check(save(7, 3, &language, sizeof(language)) == static_cast<XboxNvStatus>(0xC000000Du),
                "wrong setting type is rejected");
    ok &= Check(query(0x7777u, &type, &queried, sizeof(queried), &length) == static_cast<XboxNvStatus>(0xC000000Du),
                "unknown setting index is rejected");

    XboxNvFlush();
    ok &= Check(XboxNvInitialize(tempFile), "reload persistent EEPROM");
    queried = 0;
    ok &= Check(query(7, &type, &queried, sizeof(queried), &length) == 0 && queried == language,
                "saved setting survives reload");

    XboxNvSettingInfo info{};
    ok &= Check(XboxNvGetSettingInfo(7, &info) && info.offset == 0x90 && info.length == 4 && info.writable,
                "language layout descriptor matches EEPROM ABI");
    XboxRefurbInfo32 refurb{};
    ok &= Check(K_ExReadWriteRefurbInfo(&refurb, sizeof(refurb), 0) == static_cast<XboxNvStatus>(0xC00000BBu),
                "refurb metadata returns explicit STATUS_NOT_SUPPORTED");
    ok &= Check(K_AvSetDisplayMode(nullptr, 0, 0, 0, 0, 0) == 0xC00000BBu,
                "AV display-mode request returns explicit STATUS_NOT_SUPPORTED");
    std::uint8_t zeroWord[4]{};
    ok &= Check(XboxNvComputeChecksum(zeroWord, sizeof(zeroWord)) == 0xFFFFFFFFu,
                "XConfigChecksum zero-word reference vector");

    VirtualFree(queryCode, 0, MEM_RELEASE);
    VirtualFree(saveCode, 0, MEM_RELEASE);
    DeleteFileW(tempFile);
    if (!ok) return 1;
    std::puts("PASS: Group A EEPROM import thunks, ABI, validation, checksum and persistence");
    return 0;
}
