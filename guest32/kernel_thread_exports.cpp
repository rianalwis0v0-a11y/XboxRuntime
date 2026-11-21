#include "kernel_thread_exports.h"

#include <algorithm>
#include <array>
#include <memory>
#include <new>
#include <mutex>
#include <cstring>
#include <unordered_map>

namespace {
using KernelApcRoutine = void (__stdcall*)(void*, XboxKernelNormalRoutine*, void**, void**, void**);
using RundownRoutine = void (__stdcall*)(void*);
using NormalRoutine = void (__stdcall*)(void*, void*, void*);
using ServiceRoutine = BOOLEAN (__stdcall*)(void*, void*);
using SynchronizeRoutine = BOOLEAN (__stdcall*)(void*);

struct ApcRecord {
    HANDLE thread = nullptr;
    KernelApcRoutine kernel = nullptr;
    RundownRoutine rundown = nullptr;
    NormalRoutine normal = nullptr;
    UCHAR mode = 0;
    void* normalContext = nullptr;
};
struct ApcWork {
    void* apc;
    void* arg1;
    void* arg2;
};
struct InterruptRecord {
    ServiceRoutine service = nullptr;
    void* context = nullptr;
    ULONG vector = 0;
    UCHAR irql = 0;
    ULONG mode = 0;
    BOOLEAN shared = FALSE;
    bool connected = false;
    std::mutex synchronization;
};
std::mutex g_apcMutex;
std::unordered_map<void*, ApcRecord> g_apcs;
std::mutex g_interruptMutex;
std::unordered_map<void*, std::shared_ptr<InterruptRecord>> g_interrupts;
struct SystemInterruptRecord { bool enabled = false; ULONG mode = 0; };
std::mutex g_systemInterruptMutex;
std::array<SystemInterruptRecord, 256> g_systemInterrupts{};
thread_local UCHAR g_pendingSoftwareIrql = 0;

using PciConfigSpace = std::array<std::uint8_t, 256>;
std::mutex g_pciConfigMutex;

void StorePciDword(PciConfigSpace& config, std::size_t offset, ULONG value) {
    config[offset + 0] = static_cast<std::uint8_t>(value);
    config[offset + 1] = static_cast<std::uint8_t>(value >> 8);
    config[offset + 2] = static_cast<std::uint8_t>(value >> 16);
    config[offset + 3] = static_cast<std::uint8_t>(value >> 24);
}
PciConfigSpace MakeXboxHostBridgeConfig() {
    PciConfigSpace config{};
    StorePciDword(config, 0x00, 0x02A510DEu);
    StorePciDword(config, 0x04, 0x00100006u);
    StorePciDword(config, 0x08, 0x060000A1u);
    StorePciDword(config, 0x0C, 0x00800000u);
    StorePciDword(config, 0x10, 0x40000008u);
    StorePciDword(config, 0x34, 0x00000040u);
    StorePciDword(config, 0x40, 0x00206002u);
    StorePciDword(config, 0x44, 0x1F000217u);
    StorePciDword(config, 0x48, 0x00000014u);
    StorePciDword(config, 0x4C, 0x00000001u);
    StorePciDword(config, 0x54, 0x10000000u);
    StorePciDword(config, 0x58, 0xFFFFFFFFu);
    StorePciDword(config, 0x5C, 0xFFFFFFFFu);
    StorePciDword(config, 0x60, 0x20010008u);
    StorePciDword(config, 0x64, 0x88880120u);
    StorePciDword(config, 0x68, 0x00000010u);
    StorePciDword(config, 0x6C, 0x0F0F0F21u);
    StorePciDword(config, 0x70, 0xFFFFFFFFu);
    StorePciDword(config, 0x74, 0xFFFFFFFFu);
    StorePciDword(config, 0x78, 0xFFFFFFFFu);
    StorePciDword(config, 0x7C, 0xFFFFFFFFu);
    StorePciDword(config, 0x80, 0x00000100u);
    StorePciDword(config, 0x84, 0x07FFFFFFu);
    StorePciDword(config, 0x88, 0x00000001u);
    StorePciDword(config, 0x8C, 0x3FF6F417u);
    StorePciDword(config, 0x94, 0xF9FEFFFFu);
    StorePciDword(config, 0xA4, 0x00002001u);
    StorePciDword(config, 0xB0, 0x00000001u);
    StorePciDword(config, 0xC0, 0x00033333u);
    StorePciDword(config, 0xC4, 0x00033333u);
    StorePciDword(config, 0xC8, 0x00000013u);
    StorePciDword(config, 0xD4, 0x00000001u);
    StorePciDword(config, 0xD8, 0x07F0FFFFu);
    StorePciDword(config, 0xE0, 0x00400006u);
    StorePciDword(config, 0xE4, 0x01FF75B7u);
    return config;
}
PciConfigSpace MakeXboxHostBridgeClockConfig() {
    PciConfigSpace config{};
    config.fill(0xFFu);
    // NXDK's CRT probes bus 0, device 0, function 3, register 0x6C.
    // Divider=1 and multiplier=8 produce the Xbox 133.333 MHz FSB.
    StorePciDword(config, 0x6C, 0x00000801u);
    return config;
}
PciConfigSpace MakeNv2aPciConfig() {
    PciConfigSpace config{};
    // DVT4 PCI config-space dump: bus 1, device 0, function 0.
    StorePciDword(config, 0x00, 0x02A010DEu);
    StorePciDword(config, 0x04, 0x02B00007u);
    StorePciDword(config, 0x08, 0x030000A1u);
    StorePciDword(config, 0x0C, 0x0000F800u);
    StorePciDword(config, 0x10, 0xFD000000u);
    StorePciDword(config, 0x14, 0xF0000008u);
    StorePciDword(config, 0x18, 0x00000008u);
    StorePciDword(config, 0x34, 0x00000060u);
    StorePciDword(config, 0x3C, 0x01050103u);
    StorePciDword(config, 0x44, 0x00200002u);
    StorePciDword(config, 0x48, 0x1F000017u);
    StorePciDword(config, 0x4C, 0x1F000114u);
    StorePciDword(config, 0x54, 0x00000001u);
    StorePciDword(config, 0x58, 0x0023D6CEu);
    StorePciDword(config, 0x5C, 0x0000000Fu);
    StorePciDword(config, 0x60, 0x00024401u);
    StorePciDword(config, 0x80, 0x2B16D065u);
    return config;
}
PciConfigSpace MakeXboxAgpBridgeConfig() {
    PciConfigSpace config{};
    StorePciDword(config, 0x00, 0x01B710DEu);
    StorePciDword(config, 0x04, 0x02200007u);
    StorePciDword(config, 0x08, 0x060400A1u);
    StorePciDword(config, 0x0C, 0x5F016DF7u);
    StorePciDword(config, 0x10, 0xBF7E3FFFu);
    StorePciDword(config, 0x14, 0xBBFFFBFFu);
    StorePciDword(config, 0x18, 0x00010100u);
    StorePciDword(config, 0x1C, 0x02A000F0u);
    StorePciDword(config, 0x20, 0xFE70FD00u);
    StorePciDword(config, 0x24, 0xF3F0F000u);
    StorePciDword(config, 0x28, 0xFF3FBFFFu);
    StorePciDword(config, 0x2C, 0xAFFF7FFFu);
    StorePciDword(config, 0x30, 0x3EFFF5FDu);
    StorePciDword(config, 0x38, 0x3BDAC4F7u);
    StorePciDword(config, 0x3C, 0x0080E7F7u);
    StorePciDword(config, 0x40, 0x00000001u);
    StorePciDword(config, 0x44, 0x80000000u);
    StorePciDword(config, 0x48, 0x00000014u);
    StorePciDword(config, 0x4C, 0x00000001u);
    StorePciDword(config, 0x54, 0x10000000u);
    return config;
}
PciConfigSpace g_hostBridgePciConfig = MakeXboxHostBridgeConfig();
PciConfigSpace g_hostBridgeClockPciConfig = MakeXboxHostBridgeClockConfig();
PciConfigSpace g_agpBridgePciConfig = MakeXboxAgpBridgeConfig();
PciConfigSpace g_nv2aPciConfig = MakeNv2aPciConfig();
std::array<bool, 3> g_nv2aBarProbe{};


void CALLBACK DispatchApc(ULONG_PTR parameter) {
    std::unique_ptr<ApcWork> work(reinterpret_cast<ApcWork*>(parameter));
    if (!work) return;
    ApcRecord record{};
    {
        std::lock_guard<std::mutex> lock(g_apcMutex);
        auto it = g_apcs.find(work->apc);
        if (it == g_apcs.end()) return;
        record = it->second;
    }
    auto normal = record.normal;
    void* normalContext = record.normalContext;
    void* arg1 = work->arg1;
    void* arg2 = work->arg2;
    if (record.kernel)
        record.kernel(work->apc, &normal,
                      &normalContext, &arg1, &arg2);
    if (normal) normal(normalContext, arg1, arg2);
}
void CALLBACK AlertApc(ULONG_PTR) {}
}

extern "C" void __fastcall K_HalRequestSoftwareInterrupt(UCHAR requestIrql) {
    g_pendingSoftwareIrql = (std::max)(g_pendingSoftwareIrql, requestIrql);
}
extern "C" void __fastcall K_HalClearSoftwareInterrupt(UCHAR requestIrql) {
    if (g_pendingSoftwareIrql <= requestIrql) g_pendingSoftwareIrql = 0;
}
extern "C" BOOLEAN __stdcall K_KeAlertThread(HANDLE thread, UCHAR) {
    if (!thread || thread == INVALID_HANDLE_VALUE) return FALSE;
    return QueueUserAPC(AlertApc, thread, 0) != 0 ? TRUE : FALSE;
}
extern "C" void __stdcall K_KeInitializeApc(
    void* apc, void* thread, KernelApcRoutine kernelRoutine,
    RundownRoutine rundownRoutine, NormalRoutine normalRoutine,
    UCHAR processorMode, void* normalContext) {
    if (!apc || !thread) return;
    std::lock_guard<std::mutex> lock(g_apcMutex);
    g_apcs[apc] = ApcRecord{static_cast<HANDLE>(thread), kernelRoutine,
        rundownRoutine, normalRoutine, processorMode, normalContext};
}
extern "C" BOOLEAN __stdcall K_KeInsertQueueApc(
    void* apc, void* systemArgument1, void* systemArgument2, LONG) {
    if (!apc) return FALSE;
    ApcRecord record{};
    {
        std::lock_guard<std::mutex> lock(g_apcMutex);
        auto it = g_apcs.find(apc);
        if (it == g_apcs.end()) return FALSE;
        record = it->second;
    }
    auto* work = new (std::nothrow) ApcWork{apc, systemArgument1, systemArgument2};
    if (!work) return FALSE;
    if (!QueueUserAPC(DispatchApc, record.thread,
                      reinterpret_cast<ULONG_PTR>(work))) {
        delete work;
        return FALSE;
    }
    return TRUE;
}
extern "C" void __stdcall K_KeInitializeInterrupt(
    void* interrupt, ServiceRoutine serviceRoutine, void* serviceContext,
    ULONG vector, UCHAR irql, ULONG interruptMode, BOOLEAN shareVector) {
    if (!interrupt || !serviceRoutine) return;
    auto record = std::make_shared<InterruptRecord>();
    record->service = serviceRoutine;
    record->context = serviceContext;
    record->vector = vector;
    record->irql = irql;
    record->mode = interruptMode;
    record->shared = shareVector;
    std::lock_guard<std::mutex> lock(g_interruptMutex);
    g_interrupts[interrupt] = std::move(record);
}
extern "C" BOOLEAN __stdcall K_KeConnectInterrupt(void* interrupt) {
    if (!interrupt) return FALSE;
    std::lock_guard<std::mutex> lock(g_interruptMutex);
    auto it = g_interrupts.find(interrupt);
    if (it == g_interrupts.end() || it->second->connected) return FALSE;
    // This registers the virtual interrupt object. Physical device interrupt
    // delivery remains unavailable until a bus/interrupt-controller backend exists.
    it->second->connected = true;
    return TRUE;
}
extern "C" BOOLEAN __stdcall K_KeDisconnectInterrupt(void* interrupt) {
    if (!interrupt) return FALSE;
    std::lock_guard<std::mutex> lock(g_interruptMutex);
    auto it = g_interrupts.find(interrupt);
    if (it == g_interrupts.end() || !it->second->connected) return FALSE;
    it->second->connected = false;
    return TRUE;
}
extern "C" BOOLEAN __stdcall K_KeSynchronizeExecution(
    void* interrupt, SynchronizeRoutine synchronizeRoutine,
    void* synchronizeContext) {
    if (!interrupt || !synchronizeRoutine) return FALSE;
    std::shared_ptr<InterruptRecord> record;
    {
        std::lock_guard<std::mutex> lock(g_interruptMutex);
        auto it = g_interrupts.find(interrupt);
        if (it == g_interrupts.end() || !it->second->connected) return FALSE;
        record = it->second;
    }
    std::lock_guard<std::mutex> lock(record->synchronization);
    return synchronizeRoutine(synchronizeContext);
}

void XboxKernelResetThreadExports() {
    {
        std::lock_guard<std::mutex> lock(g_pciConfigMutex);
        g_hostBridgePciConfig = MakeXboxHostBridgeConfig();
        g_agpBridgePciConfig = MakeXboxAgpBridgeConfig();
        g_nv2aPciConfig = MakeNv2aPciConfig();
        g_nv2aBarProbe = {};
    }
    {
        std::lock_guard<std::mutex> lock(g_apcMutex);
        g_apcs.clear();
    }
    {
        std::lock_guard<std::mutex> lock(g_interruptMutex);
        g_interrupts.clear();
    }
    {
        std::lock_guard<std::mutex> lock(g_systemInterruptMutex);
        g_systemInterrupts = {};
    }
}


extern "C" void __stdcall K_HalDisableSystemInterrupt(ULONG busInterruptLevel) {
    if (busInterruptLevel >= g_systemInterrupts.size()) return;
    std::lock_guard<std::mutex> lock(g_systemInterruptMutex);
    g_systemInterrupts[busInterruptLevel].enabled = false;
}
extern "C" void __stdcall K_HalEnableSystemInterrupt(
    ULONG busInterruptLevel, ULONG interruptMode) {
    if (busInterruptLevel >= g_systemInterrupts.size()) return;
    std::lock_guard<std::mutex> lock(g_systemInterruptMutex);
    auto& record = g_systemInterrupts[busInterruptLevel];
    record.mode = interruptMode;
    record.enabled = true;
}
extern "C" ULONG __stdcall K_HalGetInterruptVector(ULONG, UCHAR* irql) {
    if (irql) *irql = 0;
    return 0; // no physical interrupt routing table is available
}
extern "C" void __stdcall K_HalReadWritePCISpace(
    ULONG busNumber, ULONG slotNumber, ULONG registerNumber,
    void* buffer, ULONG length, BOOLEAN writePciSpace) {
    if (!buffer || !length || length > 4u ||
        registerNumber >= 256u || length > 256u - registerNumber)
        return;

    // PCI_SLOT_NUMBER: device in bits 0..4, function in bits 5..7.
    PciConfigSpace* config = nullptr;
    const bool nv2a = busNumber == 1u && slotNumber == 0u;
    const bool agpBridge = busNumber == 0u && slotNumber == 0x1Eu;
    const bool hostBridgeClock = busNumber == 0u && slotNumber == 0x60u;
    if (busNumber == 0u && slotNumber == 0u)
        config = &g_hostBridgePciConfig;
    else if (hostBridgeClock)
        config = &g_hostBridgeClockPciConfig;
    else if (agpBridge)
        config = &g_agpBridgePciConfig;
    else if (nv2a)
        config = &g_nv2aPciConfig;

    std::uint8_t transfer[4]{};
    if (writePciSpace) {
        SIZE_T copied = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), buffer, transfer, length, &copied) ||
            copied != length)
            return;
        if (!config) return;

        std::lock_guard<std::mutex> lock(g_pciConfigMutex);
        if (!nv2a) return; // Host bridge is currently read-only.
        if (registerNumber == 0x10u && length == 4u) {
            const ULONG value = ULONG(transfer[0]) |
                (ULONG(transfer[1]) << 8) | (ULONG(transfer[2]) << 16) |
                (ULONG(transfer[3]) << 24);
            g_nv2aBarProbe[0] = value == 0xFFFFFFFFu;
            return;
        }
        if (registerNumber == 0x14u && length == 4u) {
            const ULONG value = ULONG(transfer[0]) |
                (ULONG(transfer[1]) << 8) | (ULONG(transfer[2]) << 16) |
                (ULONG(transfer[3]) << 24);
            g_nv2aBarProbe[1] = value == 0xFFFFFFFFu;
            return;
        }
        if (registerNumber == 0x18u && length == 4u) {
            const ULONG value = ULONG(transfer[0]) |
                (ULONG(transfer[1]) << 8) | (ULONG(transfer[2]) << 16) |
                (ULONG(transfer[3]) << 24);
            g_nv2aBarProbe[2] = value == 0xFFFFFFFFu;
            return;
        }
        for (ULONG i = 0; i < length; ++i) {
            const ULONG offset = registerNumber + i;
            if (offset == 0x04u) {
                g_nv2aPciConfig[offset] = transfer[i] & 0x07u;
            } else if (offset == 0x05u) {
                g_nv2aPciConfig[offset] = 0;
            } else if (offset == 0x06u || offset == 0x07u) {
                // PCI status bits are write-one-to-clear.
                g_nv2aPciConfig[offset] &= static_cast<std::uint8_t>(~transfer[i]);
            }
        }
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_pciConfigMutex);
        if (!config) {
            std::memset(transfer, 0xFF, length);
        } else {
            for (ULONG i = 0; i < length; ++i) {
                const ULONG offset = registerNumber + i;
                std::uint8_t value = (*config)[offset];
                if (nv2a && offset >= 0x10u && offset <= 0x1Bu) {
                    const ULONG barIndex = (offset - 0x10u) / 4u;
                    if (barIndex < 3u && g_nv2aBarProbe[barIndex]) {
                        const ULONG masks[3] = {0xFF000000u, 0xF8000008u, 0xFFF80008u};
                        value = static_cast<std::uint8_t>(
                            masks[barIndex] >> (8u * ((offset - 0x10u) & 3u)));
                    }
                }
                transfer[i] = value;
            }
        }
    }
    SIZE_T copied = 0;
    (void)WriteProcessMemory(GetCurrentProcess(), buffer, transfer, length, &copied);
}
extern "C" LONG __stdcall K_KeSaveFloatingPointState(void*) {
    return static_cast<LONG>(0xC00000BBu); // STATUS_NOT_SUPPORTED
}
extern "C" LONG __stdcall K_KeRestoreFloatingPointState(void*) {
    return static_cast<LONG>(0xC00000BBu); // STATUS_NOT_SUPPORTED
}

UCHAR XboxKernelGetPendingSoftwareIrql() { return g_pendingSoftwareIrql; }

BOOLEAN XboxKernelIsSystemInterruptEnabled(ULONG level) {
    if (level >= g_systemInterrupts.size()) return FALSE;
    std::lock_guard<std::mutex> lock(g_systemInterruptMutex);
    return g_systemInterrupts[level].enabled ? TRUE : FALSE;
}
ULONG XboxKernelGetSystemInterruptMode(ULONG level) {
    if (level >= g_systemInterrupts.size()) return 0;
    std::lock_guard<std::mutex> lock(g_systemInterruptMutex);
    return g_systemInterrupts[level].mode;
}
