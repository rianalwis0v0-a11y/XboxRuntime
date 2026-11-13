#include <cassert>
#include <cstdint>
#include "../modules/audio/audio.h"

int main() {
    assert(ModuleStart() == 0);
    assert(XboxAudio_Open(0, 2) == 87);
    assert(XboxAudio_Open(48000, 3) == 87);
    assert(XboxAudio_SubmitPcm16(nullptr, 1) == 87);
    assert(XboxAudio_SubmitPcm16(reinterpret_cast<const int16_t*>(1), 0) == 87);
    XboxAudio_Close();
    ModuleStop();
    assert(XboxAudio_SubmitPcm16(reinterpret_cast<const int16_t*>(1), 1) == 21);
    return 0;
}
