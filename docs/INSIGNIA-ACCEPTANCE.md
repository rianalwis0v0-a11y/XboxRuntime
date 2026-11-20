# XboxNet / Insignia acceptance plan

## Goal

Run a supported original-Xbox title in the 32-bit guest and establish a real
connection to Insignia. A successful compile, DNS lookup, or localhost socket
test is only an intermediate milestone.

## Stages

1. **Host transport:** TCP/UDP send and receive, DNS, timeouts, shutdown,
   nonblocking mode, and cleanup are tested against real host sockets.
2. **Xbox API compatibility:** verify XNet/XSocket calling conventions,
   structure layouts, byte order, errors, asynchronous completion, and
   guest-pointer marshalling against known XDK/NXDK definitions.
3. **XBE integration:** parse and resolve the non-kernel import libraries or
   implement a separately verified interception strategy. Do not invent
   kernel ordinals for statically linked title APIs.
4. **Service discovery:** implement the address/key and resolver behavior
   required by a selected supported title.
5. **Live connection:** run an unmodified supported title or setup assistant,
   capture DNS/TCP/UDP traffic, and verify protocol-valid replies.
6. **Multiplayer:** separately test peer-to-peer UDP, NAT behavior, and title
   session discovery.

## Current code boundary

- `guest32/network_compat.*` is a Winsock transport layer, not an Xbox API
  import resolver.
- `guest32/xnet_compat.*` supplies an initial API tranche, not every XNet,
  XSocket, or SOCKAPILIST function.
- Guest socket handles are 32-bit tokens. Native host `SOCKET` handles remain
  private to the backend.
- The current resolver tries Insignia's documented primary DNS resolver
  (46.101.64.175) for fully-qualified non-local names and falls back to the
  Windows resolver. This behavior needs live testing and should eventually be
  configurable rather than hard-coded.
- The current XBE loader branch must be integrated with this layer before a
  retail title can use it.

## Pass criteria

A milestone is PASS only when the corresponding test runs and passes. The
Insignia-connection milestone requires a real supported title to complete its
sign-in flow using real network traffic. DNS-only success, localhost tests,
stubbed functions, and compile success do not satisfy that criterion.
