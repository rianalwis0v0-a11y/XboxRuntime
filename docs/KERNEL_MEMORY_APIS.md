# Xbox kernel memory and handle API tranche

Reference: pinned NXDK revision
`XboxDev/nxdk@fafbe41b48fa00a31b79dd49ad1f86a30adcdd9e`,
principally `lib/xboxkrnl/xboxkrnl.h` and `lib/xboxkrnl/xboxdef.h`.

The guest-facing declarations in `guest32/kernel_memory.h` follow the pinned
Xbox signatures, not wider Windows NT host signatures. In particular,
`NtQueryVirtualMemory(BaseAddress, MemoryInformation)` has two arguments and
`NtDuplicateObject(SourceHandle, TargetHandle, Options)` has three arguments.
The x64 host and x86 guest continue to use separate pointer widths; guest
addresses are range-checked as 32-bit values before host VM operations.

## Implemented in this tranche

| API | Behaviour |
| --- | --- |
| NtAllocateVirtualMemory (ordinal 184) | Guest-process reserve/commit, page rounding, output base/size, rejects unsupported ZeroBits and flags |
| NtFreeVirtualMemory (ordinal 199) | Release and decommit with Xbox pointer/size updates |
| NtProtectVirtualMemory (ordinal 204) | Page-rounded protection change and previous protection |
| NtQueryVirtualMemory (ordinal 217) | Returns the guest-process 32-bit MEMORY_BASIC_INFORMATION |
| NtDuplicateObject (ordinal 197) | Same-process handle duplication and DUPLICATE_CLOSE_SOURCE |

Synchronization object creation/query, signaling, waits and timer operations remain under Kernel Developer 3 ownership. This branch deliberately does not change those implementations.

## Explicit limitations

- Cross-process handle duplication is not implemented.
- Named object namespaces and directory/symbolic-link object-manager APIs are not implemented by this tranche.
- Unsupported flags and nonzero ZeroBits fail explicitly; they are not silently ignored.
- This work does not claim full Windows NT memory-manager or Xbox object-manager equivalence.

## Verification

- `kernel_memory_guest_smoke` is compiled with the Win32 toolchain and calls
  the guest-facing functions using x86 stdcall and 32-bit pointer layouts.
- `kernel_memory_host_tests` checks 32-bit guest range rounding and overflow
  from the x64 host build.
- CI is defined in `.github/workflows/kernel-memory-apis.yml`.

A successful CI run is required before these APIs are marked verified. The
implementation and test coverage are not themselves evidence of a passing run.
