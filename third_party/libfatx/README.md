# Vendored libfatx

This directory contains the C library from **XboxDev/fatx**.

- Upstream: https://github.com/XboxDev/fatx
- Upstream revision: `c83ba2340bd2805d1832bf2071e51ebe6b99e723`
- Upstream path: `libfatx/`
- License: GPL-2.0-or-later; see [LICENSE.txt](LICENSE.txt).
- Copyright notices in each upstream source file are retained.

XboxRuntime modifications:
- `fatx.c`: changed the device open mode from `r+b` to `rb` so this integration cannot write to an Xbox disk image; also closes the device when superblock initialization fails.
- The XboxRuntime FATX module currently uses libfatx during volume opening as an independent superblock/layout validation pass. The existing bounded reader remains responsible for runtime reads and rejects malformed/cyclic chains.

Do not remove the GPL notice or treat these vendored files as MIT-only. The project keeps the upstream library's license with this component.
