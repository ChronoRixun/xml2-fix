# Classic objectives page

`[Game] ClassicObjectives=1` makes the pause menu's Objectives page (PDA mode 1) follow the
first game. It is off by default; Legends Classic 0.4.1-beta.1 (the port's beta 8) builds write it.

X-Men Legends lists the visible objectives of the current mission, completed ones ticked, and
the player reads the first unticked line. The PC PDA has seven selector slots (`icon01` ..
`icon07`; slot 7 is the secondary-objective page while one is visible). Retail maps slot n to
the n-th top-level major objective even when that objective is hidden, which leaves an empty
hole in the column, and the page opens on the last page viewed or on slot 1 - often a mission
finished long ago. An XML1 act also holds far more than seven objectives.

With the rule:

- a slot is the n-th **visible** top-level major objective (the selector walk 0x5cce98 and the
  page-text walk 0x5ce148 ask one predicate), counted from a window offset;
- when the page opens (the mode switch's call of the objectives refresh, 0x5ce484) the window
  and the page are chosen: the current objective is the first visible one not complete (the last
  one when all are); the window of six slots starts at it, or late enough to fill all six (slot 7
  always opens the optional-objectives page, 0x5ce0f8, so a seventh major is never shown); its selector
  name goes into the PDA's remembered-focus cell for mode 1 (0x8afe40), which the native refresh
  then focuses;
- the Danger Room results page (0x4c87a0) keeps the native single-slot page.

Pictures, frames, titles, step lists, the Completed toggle, input and every objective state
remain native. Nothing is saved; nothing enters the gameplay agreement (local presentation).
Every guarded site and helper is checked before any of the three patches is written, in one
transaction.

The engine draws slot n's picture at a fixed virtual-screen point (36, 346 - 45 n), 34 units
square (0x5cda36..0x5cda84); the builder places its selector frames there.

Offline checks:

```
cmake --build build --config Release --target xml2_fix xml2_classic_objectives_test
build\bin\Release\xml2_classic_objectives_test.exe <owned unpacked XMen2.exe>
```
