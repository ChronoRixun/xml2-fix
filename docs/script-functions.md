# Script function limit (`[Limits] ScriptFunctions`)

XMen2.exe resolves the function names of its scripts through one tree of 320 names. The game uses 308 of them
(289 functions and 19 builtins such as `iadd` and the comparison operators); the fix's own functions for the
X-Men Legends I port (`seatParty`, `xml2fixFeature`, `extractionPointServices`, ...) go behind them, and with them
the 320 are full. A registration past the 320th is skipped without a word, and every script statement that calls
the lost function is then dropped when the script compiles - the script carries on as if the line weren't there.

`[Limits] ScriptFunctions` raises the limit:

| Value | Meaning |
| --- | --- |
| *(no key)* | 1024 while the fix registers its own functions (`[Game] ForcedTeams`), otherwise the game's own 320 |
| `320` | the game's own limit; nothing installed |
| `321` to `4096` | the total number of names |

## How the game keeps the names

The script system (`[0x787740]`, built at the game's first script use and destroyed only at exit) holds a red-black
tree in a fixed pool of 320 nodes inside the object: node *i* at `+0x0c + 16*i` (key = the interned name), the used
count at `+0x1948`, value *i* (a pointer to the function's 16-byte entry `{handler, name, return, arguments}`) at
`+0x194c + 4*i`.

- Registration, `0x4d75a0(count, table)`: called twice, by the script system's constructor for the 19 builtins and
  by `0x49fe30` for the game's table (which the fix re-points at its own longer copy, `forced_teams.cpp`). For each
  entry it interns the name, searches the tree, and inserts it (`0x4d7310`) unless the name is there already or
  the count is 320 (`0x4d7637 cmp [edi+0x1948],0x140; je 0x4d768f`: skipped).
- Lookup, `0x4d6910(name)`: only called by the compiler's call resolution `0x4d8970` (at `0x4d8981`), once per call
  statement when a script compiles. It returns the entry or NULL; the compiler checks the argument count and types
  against the entry, copies the handler into the statement and types the statement by the return signature.
- Nothing else reads the tree's values: in the whole executable value *i* has two accessors, the insert's store
  (`0x4d738d`) and the lookup's load (`0x4d695d`). No node index reaches a compiled statement, a save or the network.

## The raise

The tree is left exactly as it is - its 320 nodes, and the index of every name the game registers, are unchanged -
and the names past it go to a table of the fix's (fixed storage, no allocation on the game's thread). Two code
sites, installed as one checked transaction (`xml1_hook_transaction`: guards, write, readback, rollback) from
`DllMain`, before the game's own code runs:

| Site | Bytes | Becomes |
| --- | --- | --- |
| `0x4d7637` | the cap's `cmp`/`je` (12 bytes) | a jump to `register_stub`: below 320 the game's insert runs untouched (`0x4d7643`); at 320 the entry goes to the fix's table and the loop moves on (`0x4d768f`) |
| `0x4d696a` | the lookup's miss epilogue (5 bytes) | a jump to `lookup_stub`: the game's epilogue, then the fix's table is asked for the name (ESI still holds its text; the argument slot holds the interned handle by then) |

The tree is always searched first, and a name already present (in the tree or the fix's table) is skipped, as
the game skips it: the first registration of a name wins. Names compare byte for byte, as the game's interned
handles do, so they stay case-sensitive.

Guards cover the whole lookup (`0x4d6910`-`0x4d6971`), the registration's frame and both resume points, and the
lookup's one caller. A refusal (another build, something patched first, the script system already registered)
writes nothing and leaves the game's 320, with one line in `xml2-fix.log`; a failed rollback refuses start-up, as
every transaction of the fix does. When names are past 320 and the raise isn't installed, the forced-teams line
says how many will be dropped.

## Online and saves

Nothing about the limit is saved or sent: compiled statements hold handler pointers, never tree indices. Both
peers of a co-op game need the same functions, and that follows from the same fix build and ini; a peer whose
raise was refused would drop the statements calling the functions past 320. The raise is not in the gameplay
agreement yet (see the 0.2.2 report: it needs a rule ID of its own; rule 1's four values are the entity layout).

## Tests

`xml2_script_functions_test [XMen2.exe]`: the `[Limits]` decisions, the patch bytes and the fix's table on their
own; with a retail executable, the game's own registration and lookup copied from it and run at their own
addresses on a script system of the test's - 330 names registered as the game has it (the last 10 are dropped)
and with the fix's stubs (all 330 resolve to their own entries; the tree's 320 values are unchanged), duplicates,
a full table, ESP and EBX/ESI/EDI around every call.

`[Test] ScriptFunctionProbes=N` (1 to 64) registers `xml2fixProbe1`..`N`, `n(i)`, behind the fix's own functions;
each call writes its argument to `xml2-fix.log` - with the fix's functions filling the 320, every probe is past it.
