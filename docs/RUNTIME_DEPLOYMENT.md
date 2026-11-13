# Runtime deployment layout

Configure and build the 64-bit Windows host with Visual Studio 2022. CMake builds `xbe_guest32.exe` separately with a nested Win32 generator, because original Xbox title code is x86 while the NV2A MMIO hook requires a 64-bit host:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Stage the runtime under `C:\XboxRuntime`:

```powershell
cmake --install build --config Release --prefix C:\XboxRuntime
```

The install tree is intentionally split as follows:

```text
C:\XboxRuntime\
  boot.exe
  system\
    main.dll
    nv2a.dll
    fatx.dll
    input.dll
    audio.dll
    xdvdfs.dll
    xbe_loader.dll
    insignia_dns.dll
    xbe_guest32.exe
```

The boot program loads `system\main.dll`; the main runtime loads its subsystem DLLs from the same `system` directory. The XBE loader starts `xbe_guest32.exe` beside itself, so that executable must remain in `system` too.

The boot picker accepts extracted `.xbe` files and raw `.iso` / `.xiso` disc images. For a disc image, the guest mounts XDVDFS, loads `default.xbe` from the image root, and routes read-only `\\Device\\Cdrom0` / `D:` file opens, reads, metadata queries, and directory enumeration through that mounted image. XDVDFS writes are rejected as media write-protected. Hard-disk FATX mounting is not yet connected to guest device paths.

The XBE loader stores the persistent EEPROM image at `system\eeprom.bin`. A newly created all-`FF` EEPROM is intentionally unprovisioned; it is not a fabricated Xbox identity and cannot be used to register with Insignia.

The startup animation and sound are optional assets under `assets\startup.avi` and `assets\startup.wav`. The project does not bundle a copyrighted Xbox dashboard or game image. Supply only XBE files and media that you are legally entitled to use.

The host is x64 and the guest is a separate native x86 process. This is a native compatibility runtime, not a static-recompilation pipeline. The current kernel export coverage, DirectSound layer, graphics integration, and title compatibility are incomplete; a successful build does not establish that a retail dashboard or arbitrary game will boot.
