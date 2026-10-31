# Building a release without local build paths

Use MSVC Win32 with a static CRT (`/MT`). GameNetworkingSockets (GNS) and
protobuf are linked into `dinput.dll`; rebuilding only the Fix cannot remove
source-location strings already compiled into their static libraries.

`scripts/build-gns.ps1` builds GNS v1.6.1 (`d534c19aa760df3fb75fd20db13ba1932b8a5463`),
vjson `2533ddd055e2cd61d44a5e6b8743acb9a33bfd5c`, and the ports from vcpkg
`3cd21e48a75f67acfef289da52893ce79144aaa1`: protobuf 6.33.4#2,
abseil 20260107.1#3 and utf8-range 6.33.4. It applies the checked-in signed-cert
patch and CA public pin. BCrypt, reference ed25519 and native ICE are retained.

The `x86-windows-static-trimmed` triplet builds Release only, with
`VCPKG_C_FLAGS` and `VCPKG_CXX_FLAGS` set to `/d1trimfile:<WorkDir>/`.
GNS uses the same compiler option. This strips the absolute prefix from
`__FILE__` and header-instantiated source locations while keeping relative
filenames and line numbers. Every target dependency is rebuilt, including
abseil and utf8-range. A distinct triplet prevents reuse of untrimmed target
packages. The output package records all library hashes in `gns-package.txt`.

By default the script keeps a desktop usable: one job, Idle priority, serial
MSBuild, and it stops if another build (MSBuild, cl, link, ninja) is running.
On a dedicated build machine pass `-Jobs <cores> -Priority Normal`; the CI
workflow does. Under GitHub Actions the other-build check is skipped.

## The GNS package

Use a checkout path with no spaces, and fresh work and output directories.
Keeping both under the checkout's `.build/` folder (ignored by git, the same
layout CI uses) means the Fix's default trim prefix also covers the package
headers. From the checkout:

```powershell
pwsh -NoProfile -File scripts/build-gns.ps1 -OutDir "$PWD/.build/gns-pkg" -WorkDir "$PWD/.build/gns-work"
```

This fetches vcpkg and GNS at the pinned commits and downloads the port
sources. The package only has to be built once per MSVC toolset.

## Fix DLL and verification

Use the generator of the Visual Studio that built the package (the script
prints it): `Visual Studio 18 2026` or `Visual Studio 17 2022`. From the
checkout:

```powershell
$generator = 'Visual Studio 18 2026'   # or 'Visual Studio 17 2022'
cmake -S . -B build -G $generator -A Win32 "-DXML2_FIX_GNS_ROOT=$PWD/.build/gns-pkg"
cmake --build build --config Release
build/bin/Release/xml2_test.exe
python -B scripts/test_check_build_paths.py
python -I -B scripts/check_build_paths.py build/bin/Release/dinput.dll
python -I -B scripts/check_release_dll.py build/bin/Release/dinput.dll --ca-pin packaging/gns/ca-pin.txt
```

`XML2_FIX_TRIM_PREFIX` (default: the checkout) is the prefix stripped from the
Fix's own source locations and from the GNS headers it includes. If the
package lives outside the checkout, pass a common parent of both, e.g.
`-DXML2_FIX_TRIM_PREFIX=C:/release` for `C:/release/xml2-fix` and
`C:/release/gns-pkg`. Slashes, backslashes and a trailing separator are all
accepted.
It trims C/C++ library targets. Native fixture executables retain `__FILE__`
because some tests locate their source inputs through it; they are not shipped
in the release DLL package.

The scan examines printable ASCII and UTF-16LE runs for a drive path: a
letter, a colon and a slash or backslash with no letter or digit before it
(`(?<![A-Za-z0-9])[A-Za-z]:[\\/]`), so URL schemes such as `https://` do not
match. It prints every match and rejects all local paths. Apparent drive
patterns in a validated x86 PE base-relocation directory are reported as
loader data; those bytes encode relocation words, not source-location
strings. Malformed relocation data receives no exception. The release DLL
checker runs this policy automatically, including when CI validates the
packaged DLL, and names a path-only failure as such.

## Offline rebuild

`-Offline` rebuilds from pinned inputs that are already on the machine and
never fetches, bootstraps or updates submodules. In the following
PowerShell example, the Fix checkout is `C:/release/xml2-fix`; run the
commands from that checkout. `$localVcpkg` is an existing vcpkg tree at the
pinned commit and `$localGns` an existing GNS checkout at v1.6.1. Local
clones copy committed files; the GNS cert patch is applied by the build
script, so do not copy a patched working tree on top.

```powershell
$work = 'C:/release/deps-work'
$pkg = 'C:/release/gns-pkg'
$localVcpkg = 'C:/offline/vcpkg'
$localGns = 'C:/offline/GameNetworkingSockets'
New-Item -ItemType Directory -Force $work | Out-Null
git -c core.autocrlf=false clone --no-hardlinks $localVcpkg "$work/vcpkg"
Copy-Item "$localVcpkg/vcpkg.exe" "$work/vcpkg/vcpkg.exe"
Copy-Item -Recurse "$localVcpkg/downloads" "$work/vcpkg/downloads"
Copy-Item -Recurse "$localVcpkg/installed" "$work/vcpkg/installed"
git -c core.autocrlf=false clone --no-hardlinks $localGns "$work/gns-src"
git -c core.autocrlf=false clone --no-hardlinks "$localGns/src/external/vjson" "$work/gns-src/src/external/vjson"
pwsh -NoProfile -File scripts/build-gns.ps1 -OutDir $pkg -WorkDir $work -Offline
```

Then configure the Fix with `-DXML2_FIX_GNS_ROOT=C:/release/gns-pkg` and
`-DXML2_FIX_TRIM_PREFIX=C:/release`.

The staged `installed/` supplies the existing x64-windows host `protoc` and
host dependencies. Target libraries are rebuilt in the new triplet.
`downloads/` must contain the matching source archives and build tools.
Required source archives (also used for utf8-range) and SHA-512:

```text
abseil-abseil-cpp-20260107.1.tar.gz
f5012885d6b6844a9cf5ed92ad5468b8757db33dfe1364bfb232fff928e06c550c7eb4557f45186a8ac4d18b178df9be267681abab4a6de40823b574afbe9960
protocolbuffers-protobuf-v33.4.tar.gz
540059a93721447cf4723bcca06e91c43a4399cb366c05bf84e9d8e2c439f3107ba17803f9d912549b54c471f2dcc4c9fc834145ec441dff31ca24f9a3543aa9
```

Offline mode invokes vcpkg with `--no-downloads --binarysource=clear`,
disables binary caches and blocks origin asset downloads. Missing
archives/tools fail the build; provide the named inputs locally before
retrying. Do not remove `-Offline` to bypass an offline failure: without it
the script fetches everything itself (the route CI uses).
