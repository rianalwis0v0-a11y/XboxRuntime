# Halo 2 networking research notes for XboxNet

## Scope and terminology

There are two different products that are easy to conflate:

- **Halo 2 for the original Xbox** used the original Xbox Live service and has its own XOnline/XNet and title-specific matchmaking behavior.
- **Halo 2 for Windows Vista (H2V)** was a Games for Windows - LIVE title. Its PC dedicated-server setup belongs to the Windows/GFWL version; it is not evidence that an original Xbox can speak to that PC server.

Do not treat the H2V dedicated server as an OG Xbox-compatible server without packet-level proof.

## What the public research says about original Xbox Halo 2

The paper *A Traffic Model for the Xbox Game Halo 2* by Sebastian Zander and Grenville Armitage analyzes Halo 2 network traffic. Its discussion of Xbox Live describes matchmaking servers selecting one of the participants to host a match. That participant remains a game client as well as the host. This is evidence for a player-hosted / peer-to-peer gameplay model, not a general-purpose dedicated gameplay server.

The Insignia team's public architectural description likewise separates centralized Xbox Live services (authentication, matchmaking, friends and related service APIs) from gameplay traffic, which is usually peer-to-peer between consoles. This is a high-level description, not a complete published wire protocol.

## H2V dedicated-server clue

Microsoft's 2007 announcement describes a Windows Vista PC running Halo 2 Vista as a dedicated server for Games for Windows - LIVE players. That establishes that H2V had a PC-hosted server mode. It does **not** establish cross-play or protocol compatibility with the original Xbox release.

Therefore, XboxNet should not copy the H2V server model as the default OG Xbox design. First identify which traffic is:
1. Xbox Live control-plane traffic (authentication, service gateway, matchmaking/session setup).
2. Title gameplay traffic (usually peer-to-peer for Halo 2 OG, based on the published traffic research and Insignia's overview).
3. H2V-only dedicated-server traffic (separate Windows/GFWL implementation).

## Engineering consequences for XboxNet

- Keep the central XboxNet backend responsible for authentication, service authorization, matchmaking, friends/presence and session coordination.
- Treat gameplay transport as a separate title-specific component. For OG Halo 2, investigate the selected-player host and peer connection flow before implementing any dedicated server.
- Do not point OG Xbox DNS records at an H2V server or claim the H2V server accepts OG Xbox clients.
- Capture and label traffic separately for original Xbox Halo 2 and Halo 2 Vista. Record source platform, title version/update, destination, transport, packet direction and whether a packet belongs to matchmaking or in-game play.
- Build a dedicated-server compatibility layer only if an original-Xbox title's protocol or a verified cross-platform mode actually requires one.

## References

- Zander and Armitage, *A Traffic Model for the Xbox Game Halo 2*, Centre for Advanced Internet Architectures, Swinburne University of Technology. The paper analyzes bandwidth, packet rate, packet lengths and inter-arrival times, and discusses the participant-host model: https://citeseerx.ist.psu.edu/document?doi=97b143fd4231be8abc6e2bf61955d6020237bbee&repid=rep1&type=pdf
- Insignia public architecture overview (team interview): https://www.gamespot.com/articles/the-og-xbox-live-is-back-from-the-dead-this-is-the-team-making-it-happen/1100-6515605/
- Microsoft, *Major Nelson's Dedicated G4W LIVE Server* (June 28, 2007), describing a PC running Halo 2 Vista as a dedicated GFWL server: https://news.xbox.com/en-us/2007/06/28/major-nelson-s-dedicated-g4w-live-server/
- Insignia Halo 2 support page: https://insignia.live/halo2

## Status

This is a research/design note, not an implementation. No H2V server binary, protocol, or Xbox-to-Vista interoperability has been verified in XboxRuntime. The next implementation step is to instrument and classify original-Xbox Halo 2 matchmaking and peer-session setup separately from gameplay packets.
