# Empty Danger Room prizes

The results screen used to announce all three award tiers even when a course
defined no prize in those slots. Legends Classic converts ordinary XML1 courses
with zero completion XP and challenge courses with their original named item in
the first slot. The two other slots have no authored reward. A blank Prize line
therefore usually means there was no reward to grant.

The native dispatcher tests all three rows. Rows with no thresholds pass, and
it marks them earned before inspecting the payload (`0x4c8f92`). The results
renderer then checks the earned/new flags, prints the award and Prize labels,
and asks the formatter for a description. With an empty payload that formatter
appends only a newline. The English strings themselves are present.

`[Game] DangerRoomPrizes=1` (the default) skips these empty result and Overview rows. Set it to
`0` to retain the original presentation. The filter follows the native
formatter's payload fields: XP, named item, trait, generated item, level,
talent, or character. All authored tiers with a payload remain visible.
The existing one-shot presentation flag is consumed even for hidden rows,
exactly as the original renderer did.

The patch changes six bytes in the results renderer at `0x4ce097` and three
five-byte Overview calls at `0x4cd154`, `0x4cd184` and `0x4cd1ea`, with guards on
the surrounding flag tests, call contract and formatter's field accesses. The dispatcher,
eligibility, course definitions, inventory and earned-state bits stay native.
The exam promotion and overall earned-XP lines are outside the filtered block.
This is presentation only and adds no gameplay-agreement rule. Existing builds
need the updated DLL; no converted content rebuild or content identity change
is required.

The Overview wrapper always runs the native formatter, retaining its scratch
buffer and item-preview resets. For an empty payload it returns an empty
display string to the caller. Real item and XP Overview text stays byte-for-byte
native; the wrapper does not change course availability or completion state.

The filter also supports W2's installed item layout: the index at reward +8
is a 16-bit field with `0xffff` as its empty value. It checks the published
item-capacity state and verifies both installed formatter jumps and their
widened instruction blocks before accepting that layout. In particular, valid
indices 255 and 511 must not be mistaken for empty 8-bit indices.

XML1 has no Titanium/Adamantium/Vibranium prize scheme. Its ordinary course
rewards and extra credit contribute to Danger Room credits, which gate courses;
challenge items and exam promotion are separate. Restoring those credits is a
separate gameplay feature, not a reason to invent rewards for XML2's empty slots.

The focused native fixture maps the owner's retail executable without launching
the game and executes its actual results loop and named-item/XP formatter:

```
xml2_danger_room_prizes_test <retail-exe> on
xml2_danger_room_prizes_test <retail-exe> negative
xml2_danger_room_prizes_test <retail-exe> off
xml2_danger_room_prizes_test <retail-exe> guard0
```

`negative` deliberately disables the fix while asserting the corrected behavior
and must fail. `overviewnegative` similarly proves the original Overview fails.
`guard0` through `guard14` each corrupt one guarded site and must
leave every image byte untouched. The positive cases cover an ordinary course,
exam promotion, a named challenge reward, repeat display, XP and three real tiers.
`wide` executes the same native formatter with the two exact W2 width recipes
in synthetic cave storage, testing item indices through 1023. `wideguard0`
through `wideguard3` corrupt the widened entries or instruction blocks and must
refuse every write. This fixture does not install the entire W2 subsystem;
validation on a real W2 game copy checks that integration separately.
