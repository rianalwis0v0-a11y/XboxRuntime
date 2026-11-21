#include "nv2a_vertex_shader.h"

#include <cassert>
#include <cstdint>

int main() {
    XboxNV2A_VERTEX_PROGRAM_INFO info{};

    // A one-instruction END program is the smallest valid NV2A program.
    const std::uint32_t endProgram[4] = {0u, 0u, 0u, 1u};
    assert(XboxNV2A_ValidateVertexProgram(endProgram, 1u, &info) == 0);
    assert(info.instruction_count == 1u);
    assert(info.input_mask == 0u);
    assert(info.output_mask == 0u);
    assert(info.constant_mask[0] == 0u);
    assert(info.constant_mask[1] == 0u);
    assert(info.constant_mask[2] == 0u);
    assert(info.uses_relative_constants == 0u);

    // Missing END must not be accepted as an executable shader.
    const std::uint32_t unterminated[4] = {0u, 0u, 0u, 0u};
    assert(XboxNV2A_ValidateVertexProgram(unterminated, 1u, &info) == 9);

    // The decoder is bounded to the physical 136-instruction program store.
    assert(XboxNV2A_ValidateVertexProgram(endProgram, 137u, &info) == 1);
    assert(XboxNV2A_ValidateVertexProgram(nullptr, 1u, &info) == 1);
    assert(XboxNV2A_ValidateVertexProgram(endProgram, 1u, nullptr) == 1);

    // END may terminate before the supplied buffer's final instruction.
    const std::uint32_t trailing[8] = {
        0u, 0u, 0u, 1u,
        0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu
    };
    assert(XboxNV2A_ValidateVertexProgram(trailing, 2u, &info) == 0);
    assert(info.instruction_count == 1u);
    return 0;
}
