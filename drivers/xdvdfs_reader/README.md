# XboxRuntime XDVDFS read-only media bridge

This is the first kernel-mode component for the Windows XDVDFS work.

## What it does

- Creates the secured control device \\\\.\\XboxXdvdfsReader.
- Exposes IOCTL_XDVDFS_READ_SECTORS.
- Reads 2048-byte logical sectors from the first optical device (\\Device\\CdRom0).
- Rejects zero-length and oversized requests.
- Has no write IOCTL and never formats or writes to a disc.

## What it does not do

This is **not yet a Windows filesystem driver**. It does not register an NT filesystem, create a drive letter, enumerate XDVDFS directories, or make Explorer browse an Xbox disc. The existing XboxRuntime XDVDFS parser is still a user-mode component. The next layer must connect that parser to this read-only sector interface, preferably through a filesystem framework such as WinFsp. WinFsp's kernel driver supplies the filesystem mount interface; this project does not pretend that a sector bridge is a complete filesystem.

It also does not create XDVDFS images. Image construction and optical-disc burning are separate user-mode tooling tasks. DVD-R media cannot be formatted or erased.

## Build

Build the x64 WDM driver with Visual Studio and the Windows 10/11 WDK. Link Wdmsec.lib for IoCreateDeviceSecure. No prebuilt or fake SYS binary is committed.

## Installation and safety

A kernel driver must be correctly signed for normal Windows loading. Do not disable Secure Boot or Windows security protections to load an unsigned test driver. Use a disposable test VM with test-signing configured by its administrator, or install a properly signed package.

The current prototype reads only from \\Device\\CdRom0. It is not production-ready until physical-drive selection, media-change handling, capacity checks, cancellation, and a complete user-mode client are implemented and tested.
