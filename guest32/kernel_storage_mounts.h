#pragma once

// Mounts optional image-backed FATX/XDVDFS volumes configured through the
// XBOX_RUNTIME_* environment variables inherited by xbe_guest32.
bool XboxKernelMountConfiguredStorage();
void XboxKernelUnmountConfiguredStorage();
