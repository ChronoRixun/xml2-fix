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

Builds run at Idle priority, one job, with MSBuild `/m:1 /nr:false`.
Wait for other builds before configuring or building. On a shared machine,
coordinate the build slot with the integration operator as well.

## Offline preparation

No source acquisition is necessary if the pinned local inputs are available.
Use a fresh work directory and output directory, with no spaces. In the
following PowerShell example, the Fix checkout is `C:/release/xml2-fix`.
Run these commands from that checkout.
`$localVcpkg` is the existing pinned vcpkg tree
and `$localGns` is the existing GNS checkout. Local clones copy committed files;
the GNS cert patch is applied by the build script, so do not copy its dirty
working-tree patch on top.

```powershell
(Get-Process -Id $PID).PriorityClass = 'Idle'
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

Offline mode never fetches, bootstraps or updates submodules. It invokes
vcpkg with `--no-downloads --binarysource=clear`, disables binary caches and
blocks origin asset downloads. Missing archives/tools fail the build; provide
the named inputs locally before retrying. Do not remove `-Offline` to bypass
an offline failure. Omitting it is the explicitly online CI acquisition route.

## Fix DLL and verification

Use the same common workspace prefix for the final project's compiler mapping,
covering both the checkout and package headers. From the Fix checkout:

```powershell
cmake -S . -B ../build -G 'Visual Studio 18 2026' -A Win32 `
  -DXML2_FIX_GNS_ROOT=C:/release/gns-pkg `
  -DXML2_FIX_TRIM_PREFIX=C:/release `
  '-DCMAKE_SHARED_LINKER_FLAGS_RELEASE=/MAP /INCREMENTAL:NO'
cmake --build ../build --config Release --target xml2_fix xml2_gns_transport_test xml2_lc_fonts_host xml2_online_menus_test xml2_online_input_wiring_test xml2_union_host -- /m:1 /nr:false /p:BuildInParallel=false /p:UseMultiToolTask=false /p:CL_MPCount=1
python -B scripts/test_check_build_paths.py
python -I -B scripts/check_build_paths.py ../build/bin/Release/dinput.dll
python -I -B scripts/check_release_dll.py ../build/bin/Release/dinput.dll --ca-pin packaging/gns/ca-pin.txt
```

The CMake default strips the checkout prefix when no common prefix is supplied.
It trims C/C++ library targets. Native fixture executables retain `__FILE__`
because some tests locate their source inputs through it; they are not shipped
in the release DLL package.
The scan examines printable ASCII and UTF-16LE runs for `[A-Za-z]:[\\/]`.
It prints every broad match and rejects all local paths. The exact public
protobuf documentation URL is intentional. Apparent drive patterns in a
validated x86 PE base-relocation directory are reported as loader data;
those bytes encode relocation words, not source-location strings. Malformed
relocation data receives no exception. The release DLL checker runs this
policy automatically, including when CI validates the packaged DLL.

Behavioral validation must also compare the previous DLL with the rebuilt
DLL through the strong synthetic union host: complete game image, generated
code, captured referenced backing allocations and 5120 capability bytes.
Use the integration's enhanced capture host with `UNION_RW_REFS` and
`UNION_EXTRA_RW_SIZE` support, and record its hash with the receipts. It
captures unguarded RW buffers needed by the strong comparison; the repository
host built above provides the startup fixture. Backing bytes must match before
their relocated addresses can be normalized. Run the existing GNS
transport cases, focused UI/font hosts, startup guards and cross-module guard
scan. The path-trimming validation used the exact 060z baseline and included
corruption controls for image/code/backing/wire differences. These native
host tests do not execute a game entry point or establish live gameplay or
relay acceptance. Preserve their commands, output and DLL hashes with the
integration receipts.
