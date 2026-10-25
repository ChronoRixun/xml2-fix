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

## Observing the decision

Useful paired observation sites are the melee decision (`0xeea60` / `0x518fd0`), the chosen-attack tail (`0xef199` / `0x519840`), and natural combo completion (`0xeec6b` / `0x5192b4`). Record AI identity, game time, AI time, deadline, target, move and combo state together. A decision call, a chosen attack, a move entry and a damaging contact are different measurements.

The original diagnostic reached these paths and its temporary hooks were removed. It ran alongside other games with slow emulation, so its event counts are branch observations, not attack-rate acceptance. No matched 30/60 FPS result is claimed here.

For resource matching, the normal energy getters read actor `+0x248` in the original (`0x2e8e0`) and `+0x288` in PC (`0x41d601`). Maximum energy is an original word at `+0x2bc`, versus a PC float at `+0x314`. Record resource overrides too; a raw field alone does not establish the getter's effective result. The initial diagnostic's current-energy offset was wrong and those readings were excluded.

The previous encounter traces also contain different enemy identities and engagement sets across trials. Counts of all observed enemies must not be substituted for the metric's engaged, living enemy exposure. Match activation before attributing the difference to scheduling.

## Fix status

No cooldown compensation or frame-cap change is justified by these observations. There is no production fix or agreement revision in this change. Any subsequently validated query-cadence intervention changes shared gameplay and needs an agreement update; the existing NPC power rule already owns query scheduling. No new rule ID is assigned.
