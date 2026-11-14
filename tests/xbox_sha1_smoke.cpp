#include "../guest32/xbox_sha1.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {
bool HashAndCompare(const char* first, std::size_t firstLength,
                    const char* second, std::size_t secondLength,
                    const char* expectedHex) {
    alignas(4) std::array<std::uint8_t, XBOX_SHA_CONTEXT_BYTES> context{};
    std::array<std::uint8_t, 20> digest{};
    K_XcSHAInit(context.data());
    K_XcSHAUpdate(context.data(),
        reinterpret_cast<const std::uint8_t*>(first),
        static_cast<std::uint32_t>(firstLength));
    if (secondLength) {
        K_XcSHAUpdate(context.data(),
            reinterpret_cast<const std::uint8_t*>(second),
            static_cast<std::uint32_t>(secondLength));
    }
    K_XcSHAFinal(context.data(), digest.data());

    static constexpr char hex[] = "0123456789ABCDEF";
    char actual[41]{};
    for (std::size_t i = 0; i < digest.size(); ++i) {
        actual[i * 2] = hex[digest[i] >> 4];
        actual[i * 2 + 1] = hex[digest[i] & 0x0F];
    }
    if (std::strcmp(actual, expectedHex) != 0) {
        std::fprintf(stderr, "SHA-1 mismatch: expected %s, got %s\n",
                     expectedHex, actual);
        return false;
    }
    return true;
}
}

int main() {
    if (!HashAndCompare("abc", 3, nullptr, 0,
                        "A9993E364706816ABA3E25717850C26C9CD0D89D"))
        return 1;
    const char* vector =
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    if (!HashAndCompare(vector, 17, vector + 17, std::strlen(vector) - 17,
                        "84983E441C3BD26EBAAE4AA1F95129E5E54670F1"))
        return 2;
    std::puts("PASS: Xbox XcSHAInit/Update/Final match SHA-1 test vectors.");
    return 0;
}
