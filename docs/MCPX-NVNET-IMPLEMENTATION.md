# Superseded: MCPX NVNet hardware emulation

This document is superseded. XboxRuntime's intended Original Xbox networking path is **API/operation translation to Windows networking**, not MCPX Ethernet hardware emulation.

See [OG-XBOX-NETWORK-TRANSLATION.md](OG-XBOX-NETWORK-TRANSLATION.md) for the current scope, existing Winsock/XNet compatibility work, and the verified XBE call-path integration blocker.

No MCPX PCI device, MMIO register bank, PHY, DMA engine, or interrupt controller should be implemented for this task.
