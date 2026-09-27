# Online play: status and next steps

_Last updated 2026-09-27._

## What works

- The fix redirects every GameSpy host the game looks up to OpenSpy, and they all resolve:
  `xmenlegpc.available`, `xmenlegpc.master`, `xmenlegpc.ms<N>`, `natneg1`, `natneg2`.
- In testing, the game reached the online menu and looked up `xmenlegpc.available.openspy.net`
  and `xmenlegpc.ms7.openspy.net` successfully.

## What doesn't work yet: the lobby

XML2's online mode is built on the GameSpy **Peer** SDK (lobby rooms on GameSpy's IRC-style chat
server). The executable contains the whole chat command table (`PRIVMSG`, `JOIN`, numeric
replies) and Peer's room-name format `#GSP!%s!…`.

1. The **Region List** screen is Peer's list of *group rooms*. The game asks the server-list
   service (`xmenlegpc.ms<N>`) for the groups of `xmenlegpc`, with the fields
   `\hostname\numwaiting\maxwaiting\numservers\numplayers`.
2. Picking a region joins that group room on the chat server, and the game list is then
   filtered with `groupid=<id>`. Hosted games advertise their `groupid` in their heartbeats.

OpenSpy supports groups: its server-list service has a `GetGroups` query, and its database
has a group table (`gameid`, `groupid`, `maxwaiting`, `name`, `other`), with lobbies set up
for other games such as the Tony Hawk series. **No groups exist for `xmenlegpc`**, so the
Region List comes back empty and no games can be found. The PS2 Online Gaming community
reports the same for the PS2 version: the region list is blank, and hosted games don't show
on OpenSpy's status pages.

`[Debug] LogNetwork=1` in `xml2-fix.ini` logs the queries the game sends and the size of
each reply. One pass through the online menu shows the exact group query and how much
OpenSpy returns.

## Options

1. **Ask OpenSpy to add regions for X-Men Legends II** (recommended first step: it fixes the
   PC and PS2 versions for everyone, with no server of our own). Draft request below.
2. **Self-host OpenSpy** (their `openspy/compose` Docker setup) with XML2 groups added to our
   own database, and point players at it with `xml2-fix.ini` → `[Online] Domain=`. Players
   need a public server and wildcard DNS (`*.our-domain` → server), or a fix option that maps
   every GameSpy host to one address. OpenSpy's repositories have no licence: we can run
   their software but must not copy their code.
3. **Our own lobby service**, as part of Ultimate Legends' planned relay, written from the
   documented GameSpy protocols. It's the most work, but gives full control and works with
   the launcher's community features.

## Draft request to OpenSpy

> **X-Men Legends II: Rise of Apocalypse (PC, `xmenlegpc`): region list is empty**
>
> Hi! XML2 PC (and PS2, per the PS2 Online Gaming list) reaches OpenSpy fine, but its online
> lobby uses GameSpy Peer group rooms: the first screen is a Region List built from the
> group query for `xmenlegpc` (fields `\hostname\numwaiting\maxwaiting\numservers\numplayers`),
> and game lists are then filtered by `groupid`. There are no groups for `xmenlegpc` on
> OpenSpy, so the list is empty and nobody can host or join.
>
> Could you add a few group rooms for `xmenlegpc`, for example "North America",
> "Europe" and "Rest of World", with a sensible `maxwaiting` (say 100)? We can test straight
> away and report back. We maintain a small open-source fix for the PC version that points
> the game at OpenSpy: github.com/ChronoRixun/xml2-fix.
>
> Thanks for keeping these games alive!
