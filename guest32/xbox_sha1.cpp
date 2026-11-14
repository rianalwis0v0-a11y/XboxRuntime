#include "xbox_sha1.h"

#include <cstring>

namespace {
struct Sha1State {
    std::uint32_t words[5];
    std::uint32_t bitCount[2]; // low word first, as used by the Xbox SHA context
    std::uint8_t buffer[64];
};
static_assert(sizeof(Sha1State) == 92, "Xbox SHA-1 state layout");

constexpr std::uint32_t RotateLeft(std::uint32_t value, unsigned count) {
    return (value << count) | (value >> (32u - count));
}

std::uint32_t ReadBigEndian(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
           (std::uint32_t(p[2]) << 8) | std::uint32_t(p[3]);
}

void WriteBigEndian(std::uint8_t* p, std::uint32_t value) {
    p[0] = static_cast<std::uint8_t>(value >> 24);
    p[1] = static_cast<std::uint8_t>(value >> 16);
    p[2] = static_cast<std::uint8_t>(value >> 8);
    p[3] = static_cast<std::uint8_t>(value);
}

void Transform(Sha1State* state, const std::uint8_t block[64]) {
    std::uint32_t schedule[80]{};
    for (unsigned i = 0; i < 16; ++i)
        schedule[i] = ReadBigEndian(block + i * 4u);
    for (unsigned i = 16; i < 80; ++i)
        schedule[i] = RotateLeft(schedule[i - 3] ^ schedule[i - 8] ^
                                 schedule[i - 14] ^ schedule[i - 16], 1);

    std::uint32_t a = state->words[0], b = state->words[1];
    std::uint32_t c = state->words[2], d = state->words[3];
    std::uint32_t e = state->words[4];
    for (unsigned i = 0; i < 80; ++i) {
        std::uint32_t f = 0, k = 0;
        if (i < 20) {
            f = (b & c) | (~b & d); k = 0x5A827999u;
        } else if (i < 40) {
            f = b ^ c ^ d; k = 0x6ED9EBA1u;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu;
        } else {
            f = b ^ c ^ d; k = 0xCA62C1D6u;
        }
        const std::uint32_t next = RotateLeft(a, 5) + f + e + k + schedule[i];
        e = d; d = c; c = RotateLeft(b, 30); b = a; a = next;
    }
    state->words[0] += a; state->words[1] += b; state->words[2] += c;
    state->words[3] += d; state->words[4] += e;
    std::memset(schedule, 0, sizeof(schedule));
}

Sha1State* StateFromXboxContext(std::uint8_t* context) {
    // The Xbox SHA_CONTEXT reserves 24 bytes before its SHA-1 working state.
    return context ? reinterpret_cast<Sha1State*>(context + 24) : nullptr;
}

void Update(Sha1State* state, const std::uint8_t* input, std::uint32_t length) {
    if (!state || (length && !input)) return;
    const std::uint64_t oldBits =
        (std::uint64_t(state->bitCount[1]) << 32) | state->bitCount[0];
    const std::uint64_t newBits = oldBits + (std::uint64_t(length) << 3);
    state->bitCount[0] = static_cast<std::uint32_t>(newBits);
    state->bitCount[1] = static_cast<std::uint32_t>(newBits >> 32);

    std::uint32_t index = static_cast<std::uint32_t>((oldBits >> 3) & 63u);
    std::uint32_t consumed = 0;
    if (index) {
        const std::uint32_t take = (length < (64u - index)) ? length : (64u - index);
        if (take) std::memcpy(state->buffer + index, input, take);
        index += take;
        consumed += take;
        if (index == 64u) Transform(state, state->buffer);
    }
    while (length - consumed >= 64u) {
        Transform(state, input + consumed);
        consumed += 64u;
    }
    if (consumed < length)
        std::memcpy(state->buffer, input + consumed, length - consumed);
}

void Final(Sha1State* state, std::uint8_t digest[20]) {
    if (!state || !digest) return;
    const std::uint64_t bitLength =
        (std::uint64_t(state->bitCount[1]) << 32) | state->bitCount[0];
    const std::uint32_t index = static_cast<std::uint32_t>((bitLength >> 3) & 63u);
    const std::uint32_t paddingLength = (index < 56u) ? (56u - index) : (120u - index);
    std::uint8_t padding[64]{};
    padding[0] = 0x80u;
    Update(state, padding, paddingLength);

    std::uint8_t encodedLength[8]{};
    for (unsigned i = 0; i < 8; ++i)
        encodedLength[7u - i] = static_cast<std::uint8_t>(bitLength >> (i * 8u));
    Update(state, encodedLength, sizeof(encodedLength));

    for (unsigned i = 0; i < 5; ++i)
        WriteBigEndian(digest + i * 4u, state->words[i]);
    std::memset(state, 0, sizeof(*state));
}
}

extern "C" void __stdcall K_XcSHAInit(std::uint8_t* context) {
    Sha1State* state = StateFromXboxContext(context);
    if (!state) return;
    state->words[0] = 0x67452301u;
    state->words[1] = 0xEFCDAB89u;
    state->words[2] = 0x98BADCFEu;
    state->words[3] = 0x10325476u;
    state->words[4] = 0xC3D2E1F0u;
    state->bitCount[0] = state->bitCount[1] = 0;
    std::memset(state->buffer, 0, sizeof(state->buffer));
}

extern "C" void __stdcall K_XcSHAUpdate(std::uint8_t* context,
                                         const std::uint8_t* input,
                                         std::uint32_t length) {
    Update(StateFromXboxContext(context), input, length);
}

extern "C" void __stdcall K_XcSHAFinal(std::uint8_t* context,
                                        std::uint8_t* digest) {
    Final(StateFromXboxContext(context), digest);
}
