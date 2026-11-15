# Virtualized Xbox system time

Branch: `work/kernel-crypto-apis`
PR: https://github.com/rianalwis0v0-a11y/XboxRuntime/pull/24
NXDK ABI reference: `fafbe41b48fa00a31b79dd49ad1f86a30adcdd9e`

## Implemented export

- Ordinal 228: `NtSetSystemTime(PLARGE_INTEGER SystemTime, PLARGE_INTEGER PreviousTime)`
- Existing ordinal 128 `KeQuerySystemTime` now reads the same guest virtual clock.
- Absolute NT timeout conversion in the guest kernel uses that guest clock as well.

## Behavior

The guest process has a 100-ns system-time offset from the host Windows clock. `NtSetSystemTime` validates the input/output spans, rejects negative time with `STATUS_INVALID_PARAMETER`, optionally returns the previous guest time, and updates only the guest offset. It never calls `SetSystemTime` or otherwise changes the host clock. The offset is reset when the title shuts down.

The prototype, optional output semantics, and documented status behavior are taken from the pinned NXDK `xboxkrnl.h`.

## Tests

The x86 stdcall smoke test checks:
- set/query and previous-time values
- negative-time rejection without changing the current guest time
- invalid input and output pointers return `STATUS_ACCESS_VIOLATION`
- the host Windows clock remains unchanged
- reset restores host-based time
