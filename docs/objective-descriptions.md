# Objective completion descriptions

`[Game] ObjectiveDescriptions=1` enables the optional `updatedescription` attribute
on mission objectives. It is off by default. Without the extension or with an empty
value, the journal keeps the original description.

The journal selects the alternate description while the objective's native completion
bit is set. Counted and scripted completions use the same state; a restored save
therefore needs no migration. Clearing completion selects the original description
again. Titles, completion notifications, XP and objective order are unchanged.

The parser hook captures bounded text keyed by the unsigned state index at objective
record +0x1a9. Objective records are sorted after parsing, so pointers are unsuitable
keys. The allocation hook clears the index before attributes are parsed, including
act reloads. It does not clear other indices: several mission files can belong to
one act. The journal reads the completion bit at the point it formats the description.
No game file or saved objective record is patched.

The supported retail executable must match every guard before any of the three hooks
is written. Both code pages are made writable before writing begins, preventing a
page-protection failure from enabling only part of the feature.

Offline checks:

```
cmake --build build --config Release --target xml2_fix xml2_objective_text_test
build\bin\Release\xml2_objective_text_test.exe
build\bin\Release\xml2_objective_text_test.exe <owned-XMen2.exe>
```

The optional executable argument verifies the code guards by reading the file; it
does not execute it. The rules cover completion, reversal, restored completion,
multiple mission files, reordered records, slot reuse, missing text and truncation.
Runtime journal refresh, automatic completion, act changes and a fresh-process save
reload still require testing. The Legends Classic builder's completion-text change
depends on this feature in the planned 1.3.2 release; neither PR is a release action.
