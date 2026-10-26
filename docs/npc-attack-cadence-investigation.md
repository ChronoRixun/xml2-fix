# NPC attack-cadence investigation

The attack-rate difference reported in issue 101 is not resolved. This investigation does not change gameplay or the agreement record. A frame-rate explanation still needs an isolated comparison with matched enemy activation and resources.

## Clock and scheduling facts

The original executable defaults `DISPLAY_OPTIONS/max_fps` to **49**, not 30: the default argument at `0x1117a` is 49, and `0x111a0..0x111aa` stores its reciprocal in client `+4`. The frame loop at `0x11390` waits against that field at `0x113f6` and `0x11413`. A live original-client read confirmed `0.020408162847161293` seconds. A diagnostic 30-FPS override was restored afterward. This is a configured ceiling; actual frame throughput can be lower. Retained isolated traces' approximately 20.4-ms clock steps agree with that ceiling, but sampling alone is not a render-FPS measurement.

The premise that the port's 60-FPS default doubles the original's 30-FPS decisions therefore does not describe this reference configuration. This does not rule out a smaller frame-cadence contribution or a nonlinear scheduling effect.

The PC frame entry tests the online session's `+0x420` byte through `0x610d20`. It selects a minimum frame time of 1/30 second at `0x401dab`, or 1/60 at `0x401db4`. The configurable frame limiter changes only the latter immediate; it preserves the online branch. See `frame_rate_rules.hpp` and the session guards in `conversations_rules.hpp`.

The common AI update is PC `0x5125a0` and original `0x10a5a0`. The original game-time getter at `0x6f240` reads the stored float at game `+0x39c`. Attack deadlines are floating-point time values, not integer frame countdowns. The common deadline setter writes AI `+0x1c`: PC `0x5382a0`, original `0xfc180`.

The reaction branch observed in the original reaches the setter at `0xf2d9f`; the corresponding PC call is `0x51e3ff`. Both use `now + 0.2 + random * 0.1` for an actor without a controller, and `now + random * 0.25` for one with a controller. The three constants match bit for bit.

The query scheduler resets its budget when the requesting AI's clock differs from its cached clock:

| Field or site | Original | PC |
|---|---:|---:|
| AI clock | `+0x51c` | `+0x550` |
| Cached query clock | `0x4fee0c` | `0x78a9e8` |
| Reset path | `0xfc9aa..0xfc9d9` | `0x50274a..0x502779` |
| Query entry | `0xfc990` | `0x502730` |
| Granted-query count write | `0xfca4b` | `0x5027ea` |

The enabled [NPC power rule](npc-power-rule.md) restores a one-query budget. It does not impose a fixed time interval on that reset. Consequently, the clock's update cadence matters even though attack deadlines use seconds. This is not strictly a guarantee of one grant per rendered frame: activation can introduce an AI whose cached time differs from the other actors' times.

## The shared query gates the brain on both engines (2026-10-24)

The melee brain does not run on every tick in either game. The fight state (AI state 4: XML1 `0x10ad70`, PC
`0x512ed0`) asks the visibility function first whenever the next attack time AI+0x1c is still in the future and the
AI has a target (XML1 `0x10b274..0x10b298`, PC `0x513552..0x513584`). If the shared query scheduler declines that
query, visibility returns "deferred" and the state returns without calling the brain (XML1 `0x10b2a7`, PC the
`if deferred return` after `0x513584`). Only when the attack time is due does the brain run without a query. The
brain (XML1 `0xeea60`, PC `0x518fd0`) then calls the power decision (`0xee260` / `0x518770`), which makes its own
visibility query (`0xee30e` / `0x518898`). The decline is final for that tick unless the AI is the reserved owner
that was just served: one budget per tick means the decision's second query in the same tick is declined.

Original ring traces agree: the engaged enemy's brain ran on a tick with its query served in 1582 of 1627 and
3711 of 3922 brain ticks (b6-original-moving-01). PC traces show the same gate: 2526 of 2673 (b-pc-moving-01).

| | X-Men Legends 1 (default.xbe) | XMen2.exe |
|---|---|---|
| Scheduler | `0xfc990`, thiscall on the AI, 7 arguments, `ret 0x1c` | `0x502730`, the same frame |
| Tick key | AI clock +0x51c against `0x4fee0c`; a change clears the count `0x4fee10`, the served owner `0x4ff9dc` and the hit byte `0x4fee18` (`0xfc9aa..0xfc9d9`) | +0x550 against `0x78a9e8`; `0x78a9ec`, `0x78aa10`, `0x78a9f4` (`0x50274a..0x502779`) |
| Queued age | AI+0x2a8 += argument 5, the weight (`0xfc9df`) | AI+0x2d8 (`0x50277f`) |
| Reservation | when the tick's budget is spent and the age exceeds the oldest `0x4fee14`, the owner `0x4fee34` = this actor's token and oldest = age (`0xfc9f7..0xfca18`). Nothing clears it at a tick change | the same with `0x78ac20` / `0x78a9f0` (`0x502797..0x5027b8`) |
| Admission | owner none, owner me, or my age above the oldest (`0xfca1d..0xfca3b`), and count below **1** (`0xfca41`) | retail: the first request of a tick skips the owner test (`0x5027bd`), count below **2** (`0x5027e0`). Rule 16 item 7 writes XML1's predicate and budget |
| Served | count+1 (`0xfca4b`), age 0; if I was the owner the reservation is released and `0x4ff9dc` = me | the same (`0x5027ea..0x502816`) |
| Declined | `0xfcba0`: the owner just served this tick gets its cached hit (AI+0x28d bit 3); everyone else gets 0 | `0x50294a`, AI+0x2b9 bit 3 |
| Callers and weights | visibility `0xfe550` (`0xfe683`, weight 1); `0xfce90` (`0xfcf14`, 30); target scan `0x104b40` (`0x104d3e`, 5 or 50 + int(100 - 0.1 x distance), only within 1000 units) | `0x505250` (`0x505448`; with rule 16 the visibility adapter, weight 1); `0x5036b0` (`0x503734`, 30); scan `0x50b560` (`0x50b768`, the same constants); XML2's cover search `0x510200` (`0x51105f`) |
| Who scans | the tick `0x10eed0` calls `0x10e230` before the state switch: AI state 0 scans with 50, 1 with 5, 2 with 5 and then becomes 3 (no more scans); only while actor+4 bit 14 is clear (set once a scan sees a hero) | `0x517370` -> `0x516930`: the same states; actor+4 bit 15 |
| `setAIActive(false)` | `0xf8440` -> `0x10ebc0`: AI state 2 | `0x516ec0`: AI state 2 |

With rule 16 the admission code of both games is the same instruction for instruction. The test
`xml2_npc_query_scheduler_test` runs both, read from the owner's files, against the same request schedules. Over
about 240,000 random requests the answers, the six globals and the queued ages match exactly; the retail port differs.

### The stale reservation, and what it did to the reference numbers

A reservation is released only when its owner is served again. If the owner stops asking, the reservation stays.
Every other AI is then served only after its own queued age passes the stored oldest age, which means once per
oldest + 1 requests. The measurement harness causes this on the original: at every window start it calls
`setAIActive(false)` on each parked enemy. Each one goes to state 2, makes one scan (weight 5 + up to 63 at
367-1000 units) after the fighter was already served that tick, and goes to state 3. The heaviest scan keeps the
reservation. In r-original-moving-02 the engaged enemy was served once per 97, 127, 183 and then 280 requests from
the first activation (44.48 s) onward; the parked enemies' ages grew with each window. Across sessions the original
declined 20-98 % of the engaged enemy's queries, with long runs. The PC declined 2-6 %, only as a second request
in a tick, so its parked enemies never left a reservation. Rule 16 itself can throttle: the PC officer bench f-pc-officer-01
shows constant runs of 4.

While the original was throttled, its powers almost stopped. Melee did not, because the brain still runs without a
query when the attack time is due (throttle.py, ring tag 15 entries):

| Original sessions | throttled time | powers / enemy-min | melee / enemy-min | free time | powers / enemy-min | melee / enemy-min |
|---|---|---|---|---|---|---|
| b6-original-moving-01 | 37 s | 0.0 | 27.3 | 123 s | **5.84** [3.0, 10.2] | 24.3 |
| moving, 5 sessions pooled | 368 s | 0.5 | 24-28 | 394 s | **4.87** [3.3, 6.9] | 22-27 |
| still, 3 sessions pooled | 178 s | 0-1.7 | 28-39 | 503 s | **5.97** [4.4, 7.9] | 25-31 |

So the "4.5 powers / enemy-min, PC-free" moving reference partly measures the harness. The comparable original
value is the free-time one, or a run in which no parked enemy can scan. Brackets are Poisson 95 % intervals.

No engine change follows from this. Throttling the port to match would copy a harness effect into normal play,
where both engines already share the code. Rule 16 stays at revision 10 and the agreement record is unchanged.
What remains open is why the PC's parked enemies left no reservation: actor+4 bit 15 is set (alerted), or actor+0x3d9 bit 5
differs from XML1's actor+0x335 bit 7. This needs a logged staging run.

## Observing the decision

Useful paired observation sites are the melee decision (`0xeea60` / `0x518fd0`), the chosen-attack tail (`0xef199` / `0x519840`), and natural combo completion (`0xeec6b` / `0x5192b4`). Record AI identity, game time, AI time, deadline, target, move and combo state together. A decision call, a chosen attack, a move entry and a damaging contact are different measurements.

The original diagnostic reached these paths and its temporary hooks were removed. It ran alongside other games with slow emulation, so its event counts are branch observations, not attack-rate acceptance. No matched 30/60 FPS result is claimed here.

For resource matching, the normal energy getters read actor `+0x248` in the original (`0x2e8e0`) and `+0x288` in PC (`0x41d601`). Maximum energy is an original word at `+0x2bc`, versus a PC float at `+0x314`. Record resource overrides too; a raw field alone does not establish the getter's effective result. The initial diagnostic's current-energy offset was wrong and those readings were excluded.

The previous encounter traces also contain different enemy identities and engagement sets across trials. Counts of all observed enemies must not be substituted for the metric's engaged, living enemy exposure. Match activation before attributing the difference to scheduling.

## Fix status

No cooldown compensation or frame-cap change is justified by these observations. There is no production fix or agreement revision in this change. Any subsequently validated query-cadence intervention changes shared gameplay and needs an agreement update; the existing NPC power rule already owns query scheduling. No new rule ID is assigned.
