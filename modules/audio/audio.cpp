#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>
#include "audio.h"

namespace {
constexpr uint32_t kOk = 0;
constexpr uint32_t kInvalidArgument = 87;
constexpr uint32_t kNotReady = 21;
constexpr uint32_t kNoDevice = 1297;
constexpr size_t kBufferCount = 4;

struct Buffer {
    std::vector<uint8_t> bytes;
    WAVEHDR header{};
    bool prepared = false;
};

std::mutex g_mutex;
HWAVEOUT g_wave = nullptr;
Buffer g_buffers[kBufferCount];
size_t g_nextBuffer = 0;
uint32_t g_sampleRate = 0;
uint16_t g_channels = 0;
bool g_started = false;

void ResetBuffers() {
    for (auto& buffer : g_buffers) {
        if (buffer.prepared && g_wave) {
            waveOutUnprepareHeader(g_wave, &buffer.header, sizeof(WAVEHDR));
        }
        buffer.header = {};
        buffer.prepared = false;
        buffer.bytes.clear();
    }
    g_nextBuffer = 0;
}

void CloseWaveLocked() {
    if (!g_wave) return;
    waveOutReset(g_wave);
    ResetBuffers();
    waveOutClose(g_wave);
    g_wave = nullptr;
    g_sampleRate = 0;
    g_channels = 0;
}
}

extern "C" int __cdecl ModuleStart(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_started = true;
    return 0;
}

extern "C" void __cdecl ModuleStop(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    CloseWaveLocked();
    g_started = false;
}

extern "C" uint32_t __cdecl XboxAudio_Open(uint32_t sampleRate, uint16_t channels) {
    if (sampleRate < 8000 || sampleRate > 192000 || (channels != 1 && channels != 2))
        return kInvalidArgument;

    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_started) return kNotReady;
    CloseWaveLocked();

    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = channels;
    format.nSamplesPerSec = sampleRate;
    format.wBitsPerSample = 16;
    format.nBlockAlign = static_cast<WORD>(channels * sizeof(int16_t));
    format.nAvgBytesPerSec = sampleRate * format.nBlockAlign;
    format.cbSize = 0;

    const MMRESULT result = waveOutOpen(&g_wave, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL);
    if (result != MMSYSERR_NOERROR) {
        g_wave = nullptr;
        return kNoDevice;
    }
    g_sampleRate = sampleRate;
    g_channels = channels;
    return kOk;
}

extern "C" uint32_t __cdecl XboxAudio_SubmitPcm16(const int16_t* samples, uint32_t frameCount) {
    if (!samples || frameCount == 0 || frameCount > (1u << 20))
        return kInvalidArgument;

    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_started || !g_wave || !g_channels) return kNotReady;

    Buffer& buffer = g_buffers[g_nextBuffer];
    if (buffer.prepared) {
        while ((buffer.header.dwFlags & WHDR_DONE) == 0) Sleep(1);
        if (waveOutUnprepareHeader(g_wave, &buffer.header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR)
            return kNotReady;
        buffer.prepared = false;
    }

    const size_t byteCount = static_cast<size_t>(frameCount) * g_channels * sizeof(int16_t);
    try {
        buffer.bytes.resize(byteCount);
    } catch (...) {
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    std::memcpy(buffer.bytes.data(), samples, byteCount);

    buffer.header = {};
    buffer.header.lpData = reinterpret_cast<LPSTR>(buffer.bytes.data());
    buffer.header.dwBufferLength = static_cast<DWORD>(byteCount);
    if (waveOutPrepareHeader(g_wave, &buffer.header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR)
        return kNotReady;
    buffer.prepared = true;

    if (waveOutWrite(g_wave, &buffer.header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
        waveOutUnprepareHeader(g_wave, &buffer.header, sizeof(WAVEHDR));
        buffer.prepared = false;
        return kNotReady;
    }
    g_nextBuffer = (g_nextBuffer + 1) % kBufferCount;
    return kOk;
}

extern "C" uint32_t __cdecl XboxAudio_SetVolume(uint16_t left, uint16_t right) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_started || !g_wave) return kNotReady;
    const DWORD volume = static_cast<DWORD>(left) |
                         (static_cast<DWORD>(right) << 16);
    return waveOutSetVolume(g_wave, volume) == MMSYSERR_NOERROR ? kOk : kNotReady;
}

extern "C" void __cdecl XboxAudio_Close(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    CloseWaveLocked();
}

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }
