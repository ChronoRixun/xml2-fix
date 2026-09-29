# Online play: status and next steps

_Last updated 2026-09-29._

## What works (tested)

- **The redirect.** The fix points every GameSpy host the game looks up at OpenSpy, and they
  all resolve: `xmenlegpc.available`, `xmenlegpc.master`, `xmenlegpc.ms<N>`, `natneg1`, `natneg2`.
- **Hosting on openspy.net.** With the fix, Play Online -> Host Game -> Post Game registers the
  game (challenge and keepalive replies from OpenSpy's master), and it appears on
  openspy.net's server list for "X-Men Legends PC" (`xmenlegpc`).
- **Joining and playing.** Two copies of the game on one PC, through a private OpenSpy stack:
  Join Game -> Search listed the hosted game, Join put both in the lobby, and Start Game ran the
  campaign in sync for both players (1P and 2P heroes). The Join screen also offers *Connect by
  IP* ([Del]) and finds games on the same network by broadcast (UDP 5165).
- **No region step on PC.** Join Game's Search asks the server list for games with
  `groupid is null`; the PC version never shows a Region List. OpenSpy having no groups for
  `xmenlegpc` therefore doesn't matter on PC (the old "empty Region List" reports are from the
  PS2 version and from 2012-2013).

## What was wrong: the game's own address

The game's own address (the Play Online screen's LocalIP, which is display-only; its game socket
on UDP 5165; the heartbeat's `localip0`) is the first address Windows gives for the PC's name
(`gethostbyname("localhost")` -> the PC's name -> `h_addr_list[0]`, 0x615d30). On a PC with
WSL / Hyper-V / Docker / VPN adapters that is often a virtual adapter's address, from which the
heartbeats never reach the internet: the game then never registers, with no error on screen.
The fix puts the address Windows reaches the internet from first (`[Online] LocalIP=auto`, the
default; `local_ip_rules.hpp` has the call sites). With it, hosting registered on openspy.net;
without it, the same PC's heartbeats got no reply.

## Not tested yet

- **A join across two different home networks.** Both tested players were on one PC. A join
  from another network goes through NAT negotiation (`natneg1`/`natneg2`, UDP 27901); if it
  fails, *Connect by IP* over a VPN such as Tailscale is the fallback. Reports welcome.
- Three or four players.

## Notes

- `[Debug] LogNetwork=1` in `xml2-fix.ini` logs the queries the game sends and the size of each
  reply.
- **No chat server.** Despite the GameSpy Peer code in the executable, XMen2.exe never connects to
  GameSpy's chat server: it has no `peerchat` host name, and its only TCP connection is the server
  list's (`connect` at 0x63f714, to `%s.ms%d.gamespy.com` port 28910); its other sockets are UDP
  (QR2, natneg, the game's own traffic).
- **Mods that are another game** (such as the X-Men Legends I port) should set
  `[Online] GameVersion` so their hosts and XML2's never see each other's games.

## Testing against a private OpenSpy

`[Online] Server=<IPv4>` sends every GameSpy name the game looks up to one address, so a private
OpenSpy stack (their `openspy/compose` images in Docker) can stand in for openspy.net without DNS.
The game binds its game socket to its LocalIP, so publish the stack's ports on an address that
LocalIP can reach (with `LocalIP` set to match), not only on 127.0.0.1. Two games on one PC (two
copies of the game folder) can each be driven through their own test pipe with `[Test] PipeName`.
