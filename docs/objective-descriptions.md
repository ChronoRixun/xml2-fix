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
one act. Both primary and secondary journal renderers read the completion bit at the point
they format the description. The primary hook also supplies the selected text's first
byte to the native empty-description test.
No game file or saved objective record is patched.

The supported retail executable must match every guard before any of the four hooks
is written. All three code pages are made writable before writing begins, preventing a
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
A controlled native journal comparison now displays the original description with
the feature off and the source completion description with it on. Reversal restores the original text. A counted objective reached native completion,
but its rendered display remains unverified; see the companion builder validation record.
The first runtime test exposed the separate primary renderer, which is now hooked
alongside the secondary renderer. The Legends Classic builder's completion-text change
depends on this feature, new in XML2 Fix 1.3.2.

Fresh-process save reload, act transitions, multiplayer and secondary-objective
rendering remain unverified. Runtime proof currently covers the primary journal.
