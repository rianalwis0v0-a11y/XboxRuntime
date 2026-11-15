#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "kernel_crypto.h"

namespace {
struct Sha1Context {
    std::uint32_t state[5];
    std::uint32_t count[2];
    std::uint8_t buffer[64];
};
static_assert(sizeof(Sha1Context) == 92, "SHA1 core context layout must remain 92 bytes");
constexpr std::size_t kXboxShaContextPrefix = 24;
constexpr std::size_t kXboxShaContextSize = kXboxShaContextPrefix + sizeof(Sha1Context);
struct Rc4Context { std::uint8_t state[256]; std::uint32_t i; std::uint32_t j; };
static_assert(sizeof(Rc4Context) == 264, "Xbox RC4 context layout must remain 264 bytes");

bool SpanHasProtection(const void* pointer, std::size_t length, bool write) {
    if (length == 0) return true;
    if (!pointer) return false;
    const std::uintptr_t start = reinterpret_cast<std::uintptr_t>(pointer);
    if (length > UINTPTR_MAX - start) return false;
    const std::uintptr_t end = start + length;
    std::uintptr_t cursor = start;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(reinterpret_cast<const void*>(cursor), &mbi, sizeof(mbi)) != sizeof(mbi))
            return false;
        if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
            return false;
        const DWORD protection = mbi.Protect & 0xFFu;
        const bool readable = protection == PAGE_READONLY || protection == PAGE_READWRITE ||
            protection == PAGE_WRITECOPY || protection == PAGE_EXECUTE_READ ||
            protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
        const bool writable = protection == PAGE_READWRITE || protection == PAGE_WRITECOPY ||
            protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
        if (write ? !writable : !readable) return false;
        const std::uintptr_t region = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
        if (mbi.RegionSize > UINTPTR_MAX - region) return false;
        const std::uintptr_t next = region + mbi.RegionSize;
        if (next <= cursor) return false;
        cursor = (std::min)(next, end);
    }
    return true;
}

void Transform(std::uint32_t state[5], const std::uint8_t block[64]) {
    std::uint32_t w[80]{};
    for (unsigned i = 0; i < 16; ++i) {
        const unsigned p = i * 4;
        w[i] = (std::uint32_t(block[p]) << 24) |
               (std::uint32_t(block[p + 1]) << 16) |
               (std::uint32_t(block[p + 2]) << 8) |
               std::uint32_t(block[p + 3]);
    }
    for (unsigned i = 16; i < 80; ++i) {
        const std::uint32_t v = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
        w[i] = (v << 1) | (v >> 31);
    }
    std::uint32_t a=state[0], b=state[1], c=state[2], d=state[3], e=state[4];
    for (unsigned i = 0; i < 80; ++i) {
        std::uint32_t f=0, k=0;
        if (i < 20) { f=(b & c) | ((~b) & d); k=0x5A827999u; }
        else if (i < 40) { f=b ^ c ^ d; k=0x6ED9EBA1u; }
        else if (i < 60) { f=(b & c) | (b & d) | (c & d); k=0x8F1BBCDCu; }
        else { f=b ^ c ^ d; k=0xCA62C1D6u; }
        const std::uint32_t rot=(a << 5) | (a >> 27);
        const std::uint32_t temp=rot + f + e + k + w[i];
        e=d; d=c; c=(b << 30) | (b >> 2); b=a; a=temp;
    }
    state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d; state[4]+=e;
    SecureZeroMemory(w, sizeof(w));
}
void Init(Sha1Context& c) {
    c.state[0]=0x67452301u; c.state[1]=0xEFCDAB89u;
    c.state[2]=0x98BADCFEu; c.state[3]=0x10325476u; c.state[4]=0xC3D2E1F0u;
    c.count[0]=c.count[1]=0;
    std::memset(c.buffer, 0, sizeof(c.buffer));
}
void Update(Sha1Context& c, const std::uint8_t* data, std::uint32_t length) {
    const std::uint32_t oldBits=c.count[0];
    c.count[0] += length << 3;
    if (c.count[0] < oldBits) ++c.count[1];
    c.count[1] += length >> 29;
    std::uint32_t used=(oldBits >> 3) & 63u;
    std::uint32_t offset=0;
    if (used) {
        const std::uint32_t take=(std::min)(64u-used, length);
        if (take) std::memcpy(c.buffer+used, data, take);
        used+=take; offset+=take;
        if (used == 64) Transform(c.state, c.buffer);
    }
    while (length-offset >= 64) {
        Transform(c.state, data+offset);
        offset+=64;
    }
    if (offset < length)
        std::memcpy(c.buffer, data+offset, length-offset);
}
void Final(Sha1Context& c, std::uint8_t digest[20]) {
    const std::uint64_t bits=(std::uint64_t(c.count[1]) << 32) | c.count[0];
    std::uint8_t pad[64]{0x80};
    const std::uint32_t used=(c.count[0] >> 3) & 63u;
    const std::uint32_t padLength=(used < 56) ? (56-used) : (120-used);
    Update(c, pad, padLength);
    std::uint8_t lengthBytes[8];
    for (unsigned i=0;i<8;++i) lengthBytes[7-i]=std::uint8_t(bits >> (i*8));
    Update(c, lengthBytes, 8);
    for (unsigned i=0;i<5;++i) {
        digest[i*4]=std::uint8_t(c.state[i] >> 24);
        digest[i*4+1]=std::uint8_t(c.state[i] >> 16);
        digest[i*4+2]=std::uint8_t(c.state[i] >> 8);
        digest[i*4+3]=std::uint8_t(c.state[i]);
    }
    SecureZeroMemory(&c, sizeof(c));
    SecureZeroMemory(lengthBytes, sizeof(lengthBytes));
}
void HashSegments(const std::uint8_t* a, std::uint32_t aLength,
                  const std::uint8_t* b, std::uint32_t bLength,
                  std::uint8_t digest[20]) {
    Sha1Context c{}; Init(c);
    if (aLength) Update(c, a, aLength);
    if (bLength) Update(c, b, bLength);
    Final(c, digest);
}
}

extern "C" void __stdcall K_XcSHAInit(std::uint8_t* context) {
    if (!SpanHasProtection(context, kXboxShaContextSize, true)) {
        RaiseException(EXCEPTION_ACCESS_VIOLATION, EXCEPTION_NONCONTINUABLE, 0, nullptr);
        return;
    }
    Sha1Context c{}; Init(c); std::memcpy(context + kXboxShaContextPrefix, &c, sizeof(c));
    SecureZeroMemory(&c, sizeof(c));
}
extern "C" void __stdcall K_XcSHAUpdate(std::uint8_t* context, std::uint8_t* input, std::uint32_t length) {
    if (!SpanHasProtection(context, sizeof(Sha1Context), true) ||
        !SpanHasProtection(input, length, false)) {
        RaiseException(EXCEPTION_ACCESS_VIOLATION, EXCEPTION_NONCONTINUABLE, 0, nullptr);
        return;
    }
    Sha1Context c{}; std::memcpy(&c, context + kXboxShaContextPrefix, sizeof(c));
    if (length) Update(c, input, length);
    std::memcpy(context + kXboxShaContextPrefix, &c, sizeof(c));
    SecureZeroMemory(&c, sizeof(c));
}
extern "C" void __stdcall K_XcSHAFinal(std::uint8_t* context, std::uint8_t* digest) {
    if (!SpanHasProtection(context, sizeof(Sha1Context), true) ||
        !SpanHasProtection(digest, 20, true)) {
        RaiseException(EXCEPTION_ACCESS_VIOLATION, EXCEPTION_NONCONTINUABLE, 0, nullptr);
        return;
    }
    Sha1Context c{}; std::memcpy(&c, context, sizeof(c));
    Final(c, digest);
    std::memset(context + kXboxShaContextPrefix, 0, sizeof(Sha1Context));
}
extern "C" void __stdcall K_XcRC4Key(std::uint8_t* keyStruct, std::uint32_t keyLength, std::uint8_t* key) {
    if (!keyLength || !SpanHasProtection(keyStruct, sizeof(Rc4Context), true) ||
        !SpanHasProtection(key, keyLength, false)) {
        RaiseException(0xC000000Du, EXCEPTION_NONCONTINUABLE, 0, nullptr);
        return;
    }
    Rc4Context context{};
    for (std::uint32_t i=0;i<256;++i) context.state[i]=static_cast<std::uint8_t>(i);
    std::uint32_t j=0;
    for (std::uint32_t i=0;i<256;++i) {
        j=(j + context.state[i] + key[i % keyLength]) & 0xFFu;
        const std::uint8_t temp=context.state[i];
        context.state[i]=context.state[j];
        context.state[j]=temp;
    }
    context.i=0; context.j=0;
    std::memcpy(keyStruct, &context, sizeof(context));
    SecureZeroMemory(&context, sizeof(context));
}
extern "C" void __stdcall K_XcRC4Crypt(std::uint8_t* keyStruct, std::uint32_t inputLength, std::uint8_t* input) {
    if (!SpanHasProtection(keyStruct, sizeof(Rc4Context), true) ||
        !SpanHasProtection(input, inputLength, true)) {
        RaiseException(EXCEPTION_ACCESS_VIOLATION, EXCEPTION_NONCONTINUABLE, 0, nullptr);
        return;
    }
    Rc4Context context{};
    std::memcpy(&context, keyStruct, sizeof(context));
    for (std::uint32_t n=0;n<inputLength;++n) {
        context.i=(context.i + 1u) & 0xFFu;
        context.j=(context.j + context.state[context.i]) & 0xFFu;
        const std::uint8_t temp=context.state[context.i];
        context.state[context.i]=context.state[context.j];
        context.state[context.j]=temp;
        const std::uint8_t stream=context.state[(context.state[context.i] + context.state[context.j]) & 0xFFu];
        input[n]^=stream;
    }
    std::memcpy(keyStruct, &context, sizeof(context));
    SecureZeroMemory(&context, sizeof(context));
}
extern "C" void __stdcall K_XcHMAC(std::uint8_t* key, std::uint32_t keyLength,
                                    std::uint8_t* input1, std::uint32_t input1Length,
                                    std::uint8_t* input2, std::uint32_t input2Length,
                                    std::uint8_t* digest) {
    if (!SpanHasProtection(key, keyLength, false) ||
        !SpanHasProtection(input1, input1Length, false) ||
        !SpanHasProtection(input2, input2Length, false) ||
        !SpanHasProtection(digest, 20, true)) {
        RaiseException(EXCEPTION_ACCESS_VIOLATION, EXCEPTION_NONCONTINUABLE, 0, nullptr);
        return;
    }
    std::uint8_t keyBlock[64]{}, innerPad[64], outerPad[64], innerDigest[20];
    const std::uint32_t effectiveKeyLength=(std::min)(keyLength, static_cast<std::uint32_t>(sizeof(keyBlock)));
    if (effectiveKeyLength) std::memcpy(keyBlock, key, effectiveKeyLength);
    for (unsigned i=0;i<64;++i) { innerPad[i]=keyBlock[i]^0x36; outerPad[i]=keyBlock[i]^0x5c; }
    Sha1Context inner{}; Init(inner); Update(inner, innerPad, 64);
    if (input1Length) Update(inner, input1, input1Length);
    if (input2Length) Update(inner, input2, input2Length);
    Final(inner, innerDigest);
    Sha1Context outer{}; Init(outer); Update(outer, outerPad, 64); Update(outer, innerDigest, 20);
    Final(outer, digest);
    SecureZeroMemory(keyBlock, sizeof(keyBlock)); SecureZeroMemory(innerPad, sizeof(innerPad));
    SecureZeroMemory(outerPad, sizeof(outerPad)); SecureZeroMemory(innerDigest, sizeof(innerDigest));
}
extern "C" void __stdcall K_XcDESKeyParity(std::uint8_t* key, std::uint32_t keyLength) {
    if (!SpanHasProtection(key, keyLength, true)) {
        RaiseException(EXCEPTION_ACCESS_VIOLATION, EXCEPTION_NONCONTINUABLE, 0, nullptr);
        return;
    }
    for (std::uint32_t i=0;i<keyLength;++i) {
        std::uint8_t value=key[i] & 0xFEu;
        unsigned ones=0;
        for (unsigned bit=1;bit<8;++bit) ones += (value >> bit) & 1u;
        key[i]=std::uint8_t(value | ((ones & 1u) ? 0u : 1u));
    }
}
