#include "../guest32/kernel_system.h"
#include <cstdio>

static int Fail(const char* message) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

int main() {
    XboxPsStatistics stats{};
    stats.Length = sizeof(stats);
    if (K_PsQueryStatistics(&stats) != 0) return Fail("PsQueryStatistics returned failure");
    if (stats.Length != sizeof(stats)) return Fail("Length does not match NXDK PS_STATISTICS");
    if (stats.ThreadCount == 0) return Fail("current guest process thread was not counted");
    if (stats.HandleCount == 0) return Fail("guest process handle count is zero");

    XboxPsStatistics shortStats{};
    shortStats.Length = sizeof(shortStats) - 1;
    if (K_PsQueryStatistics(&shortStats) != static_cast<LONG>(0xC0000004u))
        return Fail("short PS_STATISTICS buffer was not rejected");

    if (K_PsQueryStatistics(nullptr) != static_cast<LONG>(0xC000000Du))
        return Fail("null PS_STATISTICS pointer was not rejected");

    std::puts("PASS: PsQueryStatistics guest stdcall ABI, process counts, and invalid-buffer handling");
    return 0;
}
