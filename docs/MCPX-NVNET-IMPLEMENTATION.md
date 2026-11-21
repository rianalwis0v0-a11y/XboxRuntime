# Original Xbox MCPX Ethernet Device — Implementation Plan

## Scope

This document tracks the **console-side Ethernet hardware** for the Original Xbox. It is not the guest XNet/XSocket API, not an Xbox Live/Insignia service, and not host adapter discovery.

The target is the NVIDIA MCPX integrated Ethernet controller, exposed by the Xbox PCI topology at bus 0, device 4, function 0 (PCI BDF `00:04.0`). The commonly identified PCI IDs are NVIDIA vendor `10DE`, device `01C3` (MCP Ethernet).

## Hardware model to reproduce

The xemu Xbox machine attaches an `nvnet` PCI device at `PCI_DEVFN(4, 0)`. Its NVNet model exposes:

- PCI network-class function, interrupt pin 1.
- BAR 0: memory-mapped register block (0x400 bytes).
- BAR 1: I/O region (8 bytes; xemu currently returns zero / ignores writes there).
- MAC address registers and packet-filter controls.
- TX and RX descriptor-ring base/size/current/next registers.
- DMA reads from guest RAM for TX and DMA writes to guest RAM for RX.
- IRQ status/mask registers and PCI interrupt assertion.
- PHY registers accessed through MDIO, including link/autonegotiation state.
- A host network backend boundary below the emulated MAC.

Reference implementation: xemu's `hw/xbox/mcpx/nvnet/nvnet.c`, `nvnet_regs.h`, and `hw/xbox/xbox.c`. Register definitions are based in part on the reverse-engineered NVIDIA nForce `forcedeth` driver lineage. These are implementation references, not permission to copy GPL/LGPL code into this repository.

## Register groups to implement

The following offsets are the xemu NVNet register definitions and are the starting compatibility target. Keep them in a dedicated header and test them independently.

| Offset | Register | Purpose |
|---:|---|---|
| 0x000 | IRQ_STATUS | Latched RX/TX/error/timer/MII interrupt causes |
| 0x004 | IRQ_MASK | Enables interrupt causes |
| 0x080–0x098 | TX/RX control, status, filter | Start/stop and packet filtering |
| 0x0A8–0x0AC | MAC_ADDR_A/B | Station MAC address |
| 0x0B0–0x0BC | Multicast address/mask | Multicast filtering |
| 0x100–0x108 | TX/RX ring base and size | Guest physical descriptor rings |
| 0x110 | LINKSPEED | Link speed/status control |
| 0x11C–0x138 | Current/next descriptor and buffer addresses | Ring progress |
| 0x144 | TX_RX_CONTROL | DMA kick/idle/reset controls |
| 0x180–0x194 | MII/MDIO | PHY status and management transactions |
| 0x200–0x208 | Wakeup/pattern registers | Wake-on-LAN related state |
| 0x268–0x26C | Power capability/state | PCI power management |

Descriptor format in the reference model is 8 bytes: 32-bit guest physical buffer address, 16-bit length, and 16-bit flags. TX and RX use different flag meanings. Do not assume descriptor memory is host virtual memory; all accesses must go through the runtime's guest-physical-memory/DMA interface.

## Required implementation layers

1. **PCI function and BAR dispatch** — identify the device at 00:04.0, expose PCI configuration space, map BAR0/BAR1, and implement reset semantics.
2. **Register bank** — width/alignment-safe little-endian accesses, documented read/write masks, write-one-to-clear interrupt status where applicable, and deterministic reset values.
3. **PHY/MDIO** — model PHY address 1, basic MII registers, link state, and bounded autonegotiation behaviour.
4. **DMA descriptor engine** — validate ring sizes and guest physical ranges; walk TX/RX descriptors with wraparound; never dereference guest addresses directly; bound every packet and descriptor operation.
5. **Interrupts** — latch causes, apply masks, assert/deassert the PCI interrupt line, and test acknowledgement/re-arm behaviour.
6. **Network backend** — pass complete Ethernet frames to/from a pluggable backend (NAT, bridge, or packet tunnel). Keep Ethernet framing and MAC filtering in the device model; keep IP routing outside it.
7. **Tests** — register/reset tests, PHY/MDIO tests, TX/RX ring tests, malformed-DMA tests, interrupt tests, and an end-to-end Ethernet frame loopback test.

## Current XboxRuntime integration gap

The existing `work/xbox-networking-integration` branch has a 32-bit guest networking bridge backed by Windows Winsock. That is an API/transport compatibility layer, not a simulated MCPX NIC.

The current module ABI in `modules/module_api.h` only defines module start/stop exports. The branch's CMake graph builds the host runtime modules and a separate x86 guest process, but the inspected source does not yet expose a general PCI bus, BAR/MMIO dispatcher, guest-physical DMA service, or PCI IRQ routing API for a device module.

**Therefore do not implement a fake register-only NIC or claim packet-level console networking yet.** The next prerequisite is to add/identify shared machine services for PCI config/BAR mapping, guest physical memory DMA, interrupt routing, and a host Ethernet-frame backend. Then implement NVNet against those services.

## Out of scope

- `XNetStartup`, `XNetGetTitleXnAddr`, `XSocket`, and other guest XNet exports.
- Xbox Live, Insignia DNS/authentication/matchmaking, or any online service protocol.
- Host adapter selection as a substitute for the emulated console NIC.
- Physical electrical simulation of the Ethernet PHY/cable.

## Reference sources

- xemu Xbox networking documentation: https://xemu.app/docs/networking/
- xemu NVNet register definitions: https://github.com/xemu-project/xemu/blob/master/hw/xbox/mcpx/nvnet/nvnet_regs.h
- xemu NVNet device implementation: https://github.com/xemu-project/xemu/blob/master/hw/xbox/mcpx/nvnet/nvnet.c
- xemu Xbox PCI topology: https://github.com/xemu-project/xemu/blob/master/hw/xbox/xbox.c
- XboxDevWiki network overview: https://xboxdevwiki.net/Network
- Xbox hardware overview: https://xboxdevwiki.net/Xbox_Hardware_Overview
