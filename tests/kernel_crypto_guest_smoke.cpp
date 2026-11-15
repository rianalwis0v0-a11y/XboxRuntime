#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "kernel_crypto.h"

namespace {
int Fail(const char* label) { std::fprintf(stderr, "kernel_crypto_guest_smoke: FAIL: %s\n", label); return 1; }
bool EqualHex(const std::uint8_t* bytes, const char* hex, std::size_t count) {
    for (std::size_t i=0;i<count;++i) {
        unsigned value=0;
        if (std::sscanf(hex+i*2, "%2x", &value)!=1 || bytes[i]!=value) return false;
    }
    return true;
}
}
int main() {
    static_assert(sizeof(void*) == 4, "guest crypto ABI test must be x86");
    alignas(4) std::array<std::uint8_t,92> context{};
    std::uint8_t digest[20]{};
    const char abc[]="abc";
    K_XcSHAInit(context.data());
    K_XcSHAUpdate(context.data(), reinterpret_cast<std::uint8_t*>(const_cast<char*>(abc)), 3);
    K_XcSHAFinal(context.data(), digest);
    if (!EqualHex(digest, "a9993e364706816aba3e25717850c26c9cd0d89d", 20)) return Fail("SHA1 abc vector");
    if (std::any_of(context.begin(), context.end(), [](std::uint8_t b){return b!=0;})) return Fail("SHA1 context zeroization");

    const char longMessage[]="abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    K_XcSHAInit(context.data());
    K_XcSHAUpdate(context.data(), reinterpret_cast<std::uint8_t*>(const_cast<char*>(longMessage)), 17);
    K_XcSHAUpdate(context.data(), reinterpret_cast<std::uint8_t*>(const_cast<char*>(longMessage+17)), 39);
    K_XcSHAFinal(context.data(), digest);
    if (!EqualHex(digest, "84983e441c3bd26ebaae4aa1f95129e5e54670f1", 20))
        return Fail("SHA1 multi-update/multi-block vector");

    std::uint8_t mutableKey[20]; std::memset(mutableKey, 0x0b, sizeof(mutableKey));
    const char message[]="Hi There";
    K_XcHMAC(mutableKey, 20, reinterpret_cast<std::uint8_t*>(const_cast<char*>(message)), 8,
             nullptr, 0, digest);
    if (!EqualHex(digest, "b617318655057264e28bc0b6fb378c8ef146be00", 20)) return Fail("HMAC-SHA1 RFC 2202 vector");

    std::uint8_t desKey[8]={0x00,0x01,0x02,0x03,0x7f,0x80,0xfe,0xff};
    K_XcDESKeyParity(desKey, sizeof(desKey));
    for (std::uint8_t byte : desKey) {
        unsigned ones=0; for (unsigned bit=0;bit<8;++bit) ones+=(byte>>bit)&1u;
        if ((ones & 1u)!=1u) return Fail("DES odd parity");
    }
    std::puts("kernel_crypto_guest_smoke: PASS (x86 stdcall, SHA1 streaming, HMAC-SHA1, DES odd parity)");
    return 0;
}
