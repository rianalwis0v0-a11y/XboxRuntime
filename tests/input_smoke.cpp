#include <cstdint>
#include <cstdio>
#include "input.h"

static_assert(sizeof(XBOX_INPUT_GAMEPAD) == 18, "Original Xbox gamepad layout must be 18 bytes");
static_assert(sizeof(XBOX_INPUT_STATE) == 22, "Original Xbox input state layout must be 22 bytes");
static_assert(sizeof(XBOX_INPUT_VIBRATION) == 4, "Original Xbox vibration layout must be 4 bytes");

int main() {
    if (ModuleStart() != 0) return 1;
    XBOX_INPUT_STATE state{};
    if (XboxInput_GetState(4, &state) != 1167u) return 2;
    if (XboxInput_GetState(0, nullptr) != 1167u) return 3;
    const uint32_t mask = XboxInput_GetConnectedMask();
    if (mask & ~0x0Fu) return 4;
    ModuleStop();
    std::puts("PASS: Xbox input ABI sizes, invalid user handling, and controller mask");
    return 0;
}
