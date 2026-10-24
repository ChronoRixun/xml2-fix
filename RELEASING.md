# Stable and beta releases

Release tags are `vX.Y.Z`, `vX.Y.Z-beta.N`, or `vX.Y.Z-rc.N`, with no leading zeroes in numeric
components. The numeric base must match `project(xml2_fix VERSION ...)` in `CMakeLists.txt` and
the built DLL. `dinput.dll` keeps numeric VERSIONINFO, including its string fields; **the tag is
the authority for beta/rc identity**. `scripts/check_release_dll.py --version 1.8.0-beta.1` checks
the DLL against the numeric base `1.8.0`.

The tag workflow reads `packaging/release-notes/vX.Y.Z.md`. Beta and rc notes start with
`Beta - test build`; that base notes file is required for prereleases. Stable releases retain
the generated-notes fallback when no notes file exists. Beta and rc releases pass
`--prerelease --latest=false` to GitHub; stable releases keep the existing default flags.

Release assets use the same naming rules in both channels: `dinput.dll`, `ultimate-legends.json`,
and `xml2-fix-<tag>.zip` (for example `xml2-fix-v1.8.0-beta.1.zip`). This workflow does not publish
a SHA256SUMS file. It still uploads `gns-package.txt` as a CI artifact only.

`ultimate-legends.json` retains the existing array format for stable, beta, rc and development
builds, using the same PowerShell serializer and encoding:

```json
[["dinput.dll",1234,"UPPERCASE_SHA1","game"]]
```

Released launcher parsers require this top-level array. No version field or envelope is added.
The release tag supplies the full version, including any beta/rc suffix; no separate metadata
asset is needed. Stable users continue to resolve `releases/latest`; Beta users select the
highest semver release including prereleases and download assets from the selected tag.

## Cut a beta

First commit and review the workflow changes and put the base version and base notes in the
candidate commit. For the current
`1.8.0` base, from this repository:

```powershell
python scripts/test_release_metadata.py
git tag -a v1.8.0-beta.1 -m "XML2 Fix 1.8.0 beta 1"
git push https://github.com/ChronoRixun/xml2-fix.git refs/tags/v1.8.0-beta.1
```

Use `v1.8.0-beta.2` for the next candidate or `v1.8.0-rc.1` for an rc; do not reuse tags.
Publish the eventual stable release by tagging the reviewed stable commit `v1.8.0` using the
same process. Pushing a tag starts CI and publishes the release. These are maintainer
instructions, not authorization to publish during a local task.

The offline test script covers valid/malformed tags, base-version mismatches, notes, byte-identical manifests,
DLL suffix checks, and release flags. A real tag run must still prove MSVC/GNS compilation,
the online-DLL and zip gates, uploaded assets, and GitHub's prerelease/latest state. Check that
`releases/latest` still points to the stable tag after the beta is published.
