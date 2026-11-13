# XBE loader module

The module is split into a portable format layer and a Windows DLL adapter.

- `xbe_format.cpp/.h`: bounded XBE header/section parsing and image mapping.
- `xbe_loader.cpp/.h`: DLL exports, file I/O, lifecycle, and guest-backend handoff.
- `xbe_loader.def`: undecorated exports expected by the runtime module loader.

## Portable parser tests

The format layer has no Windows dependencies. Build and run its tests with a C++17 compiler:

```sh
c++ -std=c++17 -Wall -Wextra -Wpedantic -Werror \
  tests/xbe_format_tests.cpp modules/xbe_loader/xbe_format.cpp \
  -o xbe_format_tests
./xbe_format_tests
```

## Execution backend contract

The loader deliberately refuses to claim a successful launch unless the kernel
registers an `XBE_EXECUTION_BACKEND`. The backend owns guest virtual-memory
reservation, kernel thunk resolution, guest CPU startup, and shutdown. The
loader maps the validated image into the backend-provided contiguous guest
image window; it never calls an Xbox entry point as a Windows function.

The host starts all modules first, then calls the kernel export
`XBE_GetExecutionBackend(XBE_EXECUTION_BACKEND*)` and passes the returned table
to `XBE_RegisterExecutionBackend`. This avoids a startup-order cycle: the kernel
is initialized before the loader DLL is loaded, but the backend is registered
only after both are ready. TLS metadata and the XBE certificate/library-version
and non-kernel-import directory addresses are passed through unchanged; full TLS
allocation/callback semantics and import-directory interpretation remain the
backend's responsibility.

The parser validates the XBEH header, certificate extent, section table and
section ranges, retail/debug entry-point and thunk decoding, TLS bounds, and
library-version table bounds. The 256-byte RSA signature is retained in the
mapped header but is not cryptographically verified by this module.
