# Xbox kernel memory and object API tranche

Reference: pinned NXDK revision
`XboxDev/nxdk@fafbe41b48fa00a31b79dd49ad1f86a30adcdd9e`,
principally `lib/xboxkrnl/xboxkrnl.h` and `lib/xboxkrnl/xboxdef.h`.

The guest-facing signatures in `guest32/kernel_memory.h` follow the pinned
Xbox declarations, not the wider Windows NT host signatures. In particular:

- `NtQueryVirtualMemory(BaseAddress, MemoryInformation)` has two arguments.
- `NtDuplicateObject(SourceHandle, TargetHandle, Options)` has three arguments.
- `NtCreateEvent` and `NtCreateSemaphore` take Xbox `OBJECT_ATTRIBUTES`
  pointers and the event/count arguments shown by NXDK.
- `NtQueryEvent`, `NtQueryMutant`, `NtQuerySemaphore`, and `NtQueryTimer`
  each take a handle and a pointer to the corresponding Xbox basic-information
  structure.

Implemented in this tranche:

| API | Behaviour |
| --- | --- |
| NtAllocateVirtualMemory | x86 guest-process reserve/commit, page rounding, output base/size, rejects unsupported ZeroBits and flags |
| NtFreeVirtualMemory | release and decommit with Xbox pointer/size updates |
| NtProtectVirtualMemory | page-rounded protection change and previous protection |
| NtQueryVirtualMemory | returns the 32-bit guest-process MEMORY_BASIC_INFORMATION |
| NtCreateEvent | unnamed notification/synchronization events |
| NtCreateSemaphore | unnamed semaphores with count validation |
| NtCreateMutant | unnamed mutex/mutant objects |
| NtDuplicateObject | same-process handle duplication and DUPLICATE_CLOSE_SOURCE |
| NtQueryEvent / NtQueryMutant / NtQuerySemaphore / NtQueryTimer | native object state copied into NXDK-sized output structures |
| NtSetEvent / NtClearEvent / NtWaitForSingleObject | event state and alertable/non-alertable waits |

Named object namespaces, cross-process handle duplication, and the broader
object-manager namespace APIs remain unsupported and return explicit failure
where this tranche receives an object-attributes pointer. They are not counted
as implemented.

Validation:
- `kernel_memory_guest_smoke` is compiled with the Win32 toolchain and calls
  the guest-facing functions using x86 stdcall and 32-bit pointer layouts.
- `kernel_memory_host_tests` checks 32-bit guest range rounding and overflow
  from the x64 host build.
- CI is defined in `.github/workflows/kernel-memory-apis.yml`.

A successful build/test run is required before any API in this table is marked
verified. This document describes the intended test coverage, not a test result.
