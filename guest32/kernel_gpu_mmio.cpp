#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstring>
#include "kernel_gpu_mmio.h"
#include "kernel_compat.h"

namespace {
constexpr uint32_t kNv2aMmioBase = 0xFD000000u;
constexpr uint32_t kNv2aMmioSize = 0x01000000u;
PVOID g_handler = nullptr;

uint32_t RegisterValue(const CONTEXT& c, unsigned reg) {
    switch (reg & 7u) {
    case 0: return c.Eax;
    case 1: return c.Ecx;
    case 2: return c.Edx;
    case 3: return c.Ebx;
    case 4: return c.Esp;
    case 5: return c.Ebp;
    case 6: return c.Esi;
    default: return c.Edi;
    }
}
void SetRegisterValue(CONTEXT& c, unsigned reg, uint32_t value, unsigned width) {
    DWORD* registers[] = { &c.Eax, &c.Ecx, &c.Edx, &c.Ebx,
                           &c.Esp, &c.Ebp, &c.Esi, &c.Edi };
    DWORD& target = *registers[reg & 7u];
    if (width == 4) target = value;
    else if (width == 2) target = (target & 0xFFFF0000u) | (value & 0xFFFFu);
    else target = (target & 0xFFFFFF00u) | (value & 0xFFu);
}
uint8_t ByteRegisterValue(const CONTEXT& c, unsigned reg) {
    const uint32_t value = RegisterValue(c, reg & 3u);
    return static_cast<uint8_t>((reg & 4u) ? (value >> 8) : value);
}
void SetByteRegisterValue(CONTEXT& c, unsigned reg, uint8_t value) {
    DWORD* registers[] = { &c.Eax, &c.Ecx, &c.Edx, &c.Ebx };
    DWORD& target = *registers[reg & 3u];
    if (reg & 4u) target = (target & 0xFFFF00FFu) | (uint32_t(value) << 8);
    else target = (target & 0xFFFFFF00u) | value;
}
bool ReadCode(uint32_t address, uint8_t* bytes, SIZE_T capacity, SIZE_T* read) {
    return ReadProcessMemory(GetCurrentProcess(),
        reinterpret_cast<const void*>(static_cast<uintptr_t>(address)),
        bytes, capacity, read) != FALSE;
}
bool IsGpuRange(uint32_t address, unsigned width) {
    return width && address >= kNv2aMmioBase &&
        uint64_t(address) + width <= uint64_t(kNv2aMmioBase) + kNv2aMmioSize;
}
bool DecodeMemoryOperand(const uint8_t* code, SIZE_T available, SIZE_T& p,
                         uint8_t modrm, const CONTEXT& context,
                         uint32_t& address) {
    const unsigned mod = modrm >> 6;
    const unsigned rm = modrm & 7u;
    if (mod == 3u) return false;
    uint32_t base = 0, index = 0;
    bool hasBase = true;
    if (rm == 4u) {
        if (p >= available) return false;
        const uint8_t sib = code[p++];
        const unsigned scale = sib >> 6;
        const unsigned indexReg = (sib >> 3) & 7u;
        const unsigned baseReg = sib & 7u;
        if (indexReg != 4u) index = RegisterValue(context, indexReg) << scale;
        if (mod == 0u && baseReg == 5u) hasBase = false;
        else base = RegisterValue(context, baseReg);
    } else if (mod == 0u && rm == 5u) {
        hasBase = false;
    } else {
        base = RegisterValue(context, rm);
    }

    int32_t displacement = 0;
    if (mod == 0u && !hasBase) {
        if (p + 4 > available) return false;
        std::memcpy(&displacement, code + p, 4);
        p += 4;
    } else if (mod == 1u) {
        if (p >= available) return false;
        displacement = static_cast<int8_t>(code[p++]);
    } else if (mod == 2u) {
        if (p + 4 > available) return false;
        std::memcpy(&displacement, code + p, 4);
        p += 4;
    }
    address = (hasBase ? base : 0u) + index +
              static_cast<uint32_t>(displacement);
    return true;
}
}

LONG CALLBACK XboxKernelGpuMmioExceptionHandler(EXCEPTION_POINTERS* exception) {
#if !defined(_M_IX86)
    (void)exception;
    return EXCEPTION_CONTINUE_SEARCH;
#else
    if (!exception || !exception->ExceptionRecord || !exception->ContextRecord ||
        exception->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION ||
        exception->ExceptionRecord->NumberParameters < 2)
        return EXCEPTION_CONTINUE_SEARCH;

    const ULONG_PTR access = exception->ExceptionRecord->ExceptionInformation[0];
    const uint32_t fault = static_cast<uint32_t>(
        exception->ExceptionRecord->ExceptionInformation[1]);
    if ((access != 0 && access != 1) || fault < kNv2aMmioBase ||
        fault >= kNv2aMmioBase + kNv2aMmioSize)
        return EXCEPTION_CONTINUE_SEARCH;

    CONTEXT& context = *exception->ContextRecord;
    uint8_t code[16]{};
    SIZE_T available = 0;
    if (!ReadCode(context.Eip, code, sizeof(code), &available) || available < 2)
        return EXCEPTION_CONTINUE_SEARCH;

    SIZE_T p = 0;
    bool operand16 = false;
    for (;;) {
        if (p >= available) return EXCEPTION_CONTINUE_SEARCH;
        const uint8_t prefix = code[p];
        if (prefix == 0x66) { operand16 = true; ++p; continue; }
        if (prefix == 0x2E || prefix == 0x36 || prefix == 0x3E ||
            prefix == 0x26 || prefix == 0x64 || prefix == 0x65 ||
            prefix == 0xF2 || prefix == 0xF3) { ++p; continue; }
        if (prefix == 0x67) return EXCEPTION_CONTINUE_SEARCH;
        break;
    }
    const unsigned operandWidth = operand16 ? 2u : 4u;
    const uint8_t opcode = code[p++];
    bool readOperation = false, writeOperation = false;
    bool byteOperation = false, signExtend = false, zeroExtend = false;
    bool immediate = false, moffs = false;
    unsigned width = operandWidth, destinationRegister = 0, sourceRegister = 0;
    uint32_t immediateValue = 0, address = 0;

    if (opcode == 0xA1 || opcode == 0xA3) {
        if (p + 4 > available) return EXCEPTION_CONTINUE_SEARCH;
        std::memcpy(&address, code + p, 4); p += 4;
        moffs = true;
        readOperation = opcode == 0xA1;
        writeOperation = opcode == 0xA3;
        destinationRegister = 0;
        sourceRegister = 0;
    } else if (opcode == 0x8A || opcode == 0x88 ||
               opcode == 0x8B || opcode == 0x89 ||
               opcode == 0xC6 || opcode == 0xC7) {
        if (p + 1 > available) return EXCEPTION_CONTINUE_SEARCH;
        const uint8_t modrm = code[p++];
        const unsigned reg = (modrm >> 3) & 7u;
        if (!DecodeMemoryOperand(code, available, p, modrm, context, address))
            return EXCEPTION_CONTINUE_SEARCH;
        destinationRegister = reg;
        sourceRegister = reg;
        if (opcode == 0x8A || opcode == 0x8B) readOperation = true;
        if (opcode == 0x88 || opcode == 0x89) writeOperation = true;
        if (opcode == 0xC6 || opcode == 0xC7) {
            if (reg != 0) return EXCEPTION_CONTINUE_SEARCH;
            writeOperation = true;
            immediate = true;
        }
        byteOperation = opcode == 0x8A || opcode == 0x88 || opcode == 0xC6;
        if (byteOperation) width = 1;
    } else if (opcode == 0x0F) {
        if (p + 2 > available) return EXCEPTION_CONTINUE_SEARCH;
        const uint8_t second = code[p++];
        if (second != 0xB6 && second != 0xB7 &&
            second != 0xBE && second != 0xBF)
            return EXCEPTION_CONTINUE_SEARCH;
        const uint8_t modrm = code[p++];
        if (!DecodeMemoryOperand(code, available, p, modrm, context, address))
            return EXCEPTION_CONTINUE_SEARCH;
        destinationRegister = (modrm >> 3) & 7u;
        readOperation = true;
        width = (second == 0xB6 || second == 0xBE) ? 1u : 2u;
        signExtend = second == 0xBE || second == 0xBF;
        zeroExtend = !signExtend;
    } else {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    if (!IsGpuRange(address, width) || address != fault ||
        (access == 0 && !readOperation) || (access == 1 && !writeOperation))
        return EXCEPTION_CONTINUE_SEARCH;

    if (immediate) {
        const unsigned immediateWidth = width;
        if (p + immediateWidth > available) return EXCEPTION_CONTINUE_SEARCH;
        if (immediateWidth == 1) immediateValue = code[p];
        else if (immediateWidth == 2) {
            uint16_t v = 0; std::memcpy(&v, code + p, 2); immediateValue = v;
        } else std::memcpy(&immediateValue, code + p, 4);
        p += immediateWidth;
    }

    if (readOperation) {
        int succeeded = 0;
        uint64_t value = XboxKernelGpuReadRegisterValue(
            address - kNv2aMmioBase, width, &succeeded);
        if (!succeeded) return EXCEPTION_CONTINUE_SEARCH;
        uint32_t result = static_cast<uint32_t>(value);
        if (signExtend && width == 1) result = static_cast<uint32_t>(
            static_cast<int32_t>(static_cast<int8_t>(result)));
        else if (signExtend && width == 2) result = static_cast<uint32_t>(
            static_cast<int32_t>(static_cast<int16_t>(result)));
        else if (zeroExtend && width < 4) result &= width == 1 ? 0xFFu : 0xFFFFu;
        if (byteOperation) SetByteRegisterValue(context, destinationRegister,
                                                static_cast<uint8_t>(result));
        else SetRegisterValue(context, destinationRegister, result,
                              (signExtend || zeroExtend) ? 4u : operandWidth);
    } else {
        uint32_t value = immediateValue;
        if (!immediate) {
            if (byteOperation) value = ByteRegisterValue(context, sourceRegister);
            else value = RegisterValue(context, sourceRegister);
        }
        if (!XboxKernelGpuWriteRegisterValue(
                address - kNv2aMmioBase, width, value))
            return EXCEPTION_CONTINUE_SEARCH;
    }

    context.Eip += static_cast<DWORD>(p);
    return EXCEPTION_CONTINUE_EXECUTION;
#endif
}

BOOL XboxKernelGpuMmioInitialize(void) {
    if (g_handler) return TRUE;
    g_handler = AddVectoredExceptionHandler(1, XboxKernelGpuMmioExceptionHandler);
    return g_handler ? TRUE : FALSE;
}
void XboxKernelGpuMmioShutdown(void) {
    if (g_handler) {
        RemoveVectoredExceptionHandler(g_handler);
        g_handler = nullptr;
    }
}
