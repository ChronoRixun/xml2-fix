<#
.SYNOPSIS
  Builds the GameNetworkingSockets package the online DLL links (docs/gns-transport.md, CMakeLists.txt XML2_FIX_GNS_ROOT).

.DESCRIPTION
  Produces <OutDir>/include/steam, <OutDir>/lib (GameNetworkingSockets_s.lib, libprotobuf.lib, absl_*.lib, utf8_range.lib),
  <OutDir>/ca-pin.txt and <OutDir>/gns-package.txt (the pins this package was built from, plus SHA-256 of every lib).
  Same recipe as the 0.3.0 candidate's net030-gnslib package: GameNetworkingSockets v1.6.1 with the project's CA pinned and the
  cert-enforcement patch applied, protobuf/abseil/utf8-range from vcpkg, Win32 Release, static CRT (/MT), BCrypt crypto with the
  reference ed25519 (no OpenSSL, no libsodium), native ICE. The result is linked INTO dinput.dll: there is no GNS, protobuf or
  abseil DLL to ship next to it.

  The CI release job (.github/workflows/build.yml) runs this, caching <OutDir> under a key made of this script, packaging/gns/
  and the MSVC toolset. Run it by hand the same way:  pwsh scripts/build-gns.ps1 -OutDir C:\gns -WorkDir C:\gns-work
  then:  cmake -S . -B build -A Win32 -DXML2_FIX_GNS_ROOT=C:/gns
#>
[CmdletBinding()]
param(
	[Parameter(Mandatory)] [string] $OutDir,
	[string] $WorkDir = (Join-Path ([IO.Path]::GetTempPath()) 'xml2-fix-gns-work'),
	[int] $Jobs = [Math]::Max(2, [Environment]::ProcessorCount)
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# ---- pins ---------------------------------------------------------------------------------------------------------------
$GnsTag = 'v1.6.1'
$GnsCommit = 'd534c19aa760df3fb75fd20db13ba1932b8a5463'
$VjsonCommit = '2533ddd055e2cd61d44a5e6b8743acb9a33bfd5c'          # GNS's only submodule this build needs (src/external/vjson)
$VcpkgCommit = '3cd21e48a75f67acfef289da52893ce79144aaa1'          # microsoft/vcpkg 2026-09-26: fixes every port version below
$Triplet = 'x86-windows-static-release'                            # = x86-windows-static without the Debug half of every port
$HostTriplet = 'x64-windows-release'                               # protoc, which the build runs, is built for the host
$ExpectedPorts = [ordered]@{ 'protobuf' = '6.33.4#2'; 'abseil' = '20260107.1#3'; 'utf8-range' = '6.33.4' }
# ---------------------------------------------------------------------------------------------------------------------------

$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$PackagingDir = Join-Path $Root 'packaging/gns'
foreach ($p in $OutDir, $WorkDir) {
	if ($p -match '\s') { throw "Path with a space: '$p' (the force-include flag below is passed unquoted; use a path without spaces)" }
}
$OutDir = [IO.Path]::GetFullPath($OutDir)
$WorkDir = [IO.Path]::GetFullPath($WorkDir)
New-Item -ItemType Directory -Force $WorkDir | Out-Null

function Invoke-Checked { # $args, not parameters: "-A Win32" and "-disableMetrics" must reach the program untouched
	$exe = $args[0]
	$rest = @($args | Select-Object -Skip 1)
	Write-Host "> $exe $($rest -join ' ')"
	& $exe @rest
	if ($LASTEXITCODE -ne 0) { throw "$exe failed ($LASTEXITCODE)" }
}

# ---- toolchain: the newest Visual Studio with the x86/x64 C++ tools, for vcpkg and for CMake alike -----------------------
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path $vswhere)) { throw 'vswhere.exe not found: Visual Studio with the C++ tools is required' }
$vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vsMajor = [int](& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion).Split('.')[0]
if (-not $vsPath) { throw 'no Visual Studio with the x86/x64 C++ tools found' }
$generator = switch ($vsMajor) { 18 { 'Visual Studio 18 2026' } 17 { 'Visual Studio 17 2022' } default { throw "Visual Studio $vsMajor is not one this script was written for" } }
$toolsetVersion = (Get-Content (Join-Path $vsPath 'VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt') -TotalCount 1).Trim()
Write-Host "Toolchain: $generator ($vsPath), MSVC $toolsetVersion"
$env:VCPKG_VISUAL_STUDIO_PATH = $vsPath
$env:VCPKG_DISABLE_METRICS = '1'
$env:VCPKG_MAX_CONCURRENCY = "$Jobs"

# ---- vcpkg at the pinned commit -------------------------------------------------------------------------------------------
$vcpkgDir = Join-Path $WorkDir 'vcpkg'
if (-not (Test-Path (Join-Path $vcpkgDir '.git'))) {
	New-Item -ItemType Directory -Force $vcpkgDir | Out-Null
	Invoke-Checked git -C $vcpkgDir init -q
	Invoke-Checked git -C $vcpkgDir remote add origin https://github.com/microsoft/vcpkg.git
}
Invoke-Checked git -C $vcpkgDir fetch -q --depth 1 origin $VcpkgCommit
Invoke-Checked git -C $vcpkgDir -c advice.detachedHead=false checkout -q -f FETCH_HEAD
if ((& git -C $vcpkgDir rev-parse HEAD) -ne $VcpkgCommit) { throw 'vcpkg is not at the pinned commit' }
if (-not (Test-Path (Join-Path $vcpkgDir 'vcpkg.exe'))) { Invoke-Checked (Join-Path $vcpkgDir 'bootstrap-vcpkg.bat') -disableMetrics }
$vcpkg = Join-Path $vcpkgDir 'vcpkg.exe'

$tripletDir = Join-Path $WorkDir 'triplets'
New-Item -ItemType Directory -Force $tripletDir | Out-Null
Set-Content -Encoding ascii (Join-Path $tripletDir "$Triplet.cmake") @'
set(VCPKG_TARGET_ARCHITECTURE x86)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_BUILD_TYPE release)
'@
Set-Content -Encoding ascii (Join-Path $tripletDir "$HostTriplet.cmake") @'
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_BUILD_TYPE release)
'@
Invoke-Checked $vcpkg install "protobuf:$Triplet" "--triplet=$Triplet" "--host-triplet=$HostTriplet" "--overlay-triplets=$tripletDir" --clean-after-build

# the ports must be the versions the package was proven with
$installedJson = & $vcpkg list 2>&1 | Out-String
foreach ($port in $ExpectedPorts.Keys) {
	$want = $ExpectedPorts[$port]
	$line = ($installedJson -split "`r?`n" | Where-Object { $_ -match "^$([regex]::Escape($port)):$([regex]::Escape($Triplet))\s" } | Select-Object -First 1)
	$ver = if ($line) { ($line -split '\s+')[1] } else { '' }
	if ($ver -ne $want) { throw "vcpkg port $port is '$ver', expected '$want'" }
	Write-Host "port $port $ver"
}
$vcpkgInstalled = Join-Path $vcpkgDir "installed/$Triplet"

# ---- GameNetworkingSockets at the pinned commit, patched ------------------------------------------------------------------
$gnsSrc = Join-Path $WorkDir 'gns-src'
if (Test-Path $gnsSrc) { Remove-Item -Recurse -Force $gnsSrc }
Invoke-Checked git -c core.autocrlf=false -c advice.detachedHead=false clone -q --depth 1 --branch $GnsTag https://github.com/ValveSoftware/GameNetworkingSockets.git $gnsSrc
if ((& git -C $gnsSrc rev-parse 'HEAD^{commit}') -ne $GnsCommit) { throw "GameNetworkingSockets $GnsTag is not commit $GnsCommit" }
Invoke-Checked git -C $gnsSrc -c core.autocrlf=false submodule update -q --init --depth 1 src/external/vjson
if ((& git -C (Join-Path $gnsSrc 'src/external/vjson') rev-parse HEAD) -ne $VjsonCommit) { throw 'vjson is not the pinned commit' }
Invoke-Checked git -C $gnsSrc -c core.autocrlf=false apply --check (Join-Path $PackagingDir 'gns-p2p-require-signed-cert.patch')
Invoke-Checked git -C $gnsSrc -c core.autocrlf=false apply (Join-Path $PackagingDir 'gns-p2p-require-signed-cert.patch')

# the project CA, compiled into the library (STEAMNETWORKINGSOCKETS_HARDCODED_ROOT_CA_KEY); the private key is never in a repo
$caPin = (Get-Content (Join-Path $PackagingDir 'ca-pin.txt') -TotalCount 1).Trim()
if ($caPin -notmatch '^ssh-ed25519 [A-Za-z0-9+/=]+ ID\d+$') { throw "packaging/gns/ca-pin.txt is not an ssh-ed25519 line with an ID: '$caPin'" }
$caHeader = (Join-Path $WorkDir 'ca_key.h').Replace('\', '/')
Set-Content -Encoding ascii $caHeader "#define STEAMNETWORKINGSOCKETS_HARDCODED_ROOT_CA_KEY `"$caPin`""

$gnsBuild = Join-Path $gnsSrc 'build'
$vcpkgToolchain = (Join-Path $vcpkgDir 'scripts/buildsystems/vcpkg.cmake').Replace('\', '/')
Invoke-Checked cmake -S $gnsSrc -B $gnsBuild -G $generator -A Win32 `
	"-DCMAKE_TOOLCHAIN_FILE=$vcpkgToolchain" "-DVCPKG_TARGET_TRIPLET=$Triplet" "-DVCPKG_HOST_TRIPLET=$HostTriplet" `
	"-DVCPKG_OVERLAY_TRIPLETS=$($tripletDir.Replace('\', '/'))" -DVCPKG_MANIFEST_MODE=OFF `
	-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -DUSE_CRYPTO=BCrypt -DUSE_CRYPTO25519=Reference -DENABLE_ICE=ON `
	-DBUILD_TESTS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_TOOLS=OFF -DBUILD_STATIC_LIB=ON -DBUILD_SHARED_LIB=OFF `
	-DProtobuf_USE_STATIC_LIBS=ON "-DCMAKE_CXX_FLAGS=/FI$caHeader" "-DCMAKE_C_FLAGS=/FI$caHeader"
Invoke-Checked cmake --build $gnsBuild --config Release --target GameNetworkingSockets_s --parallel $Jobs

# ---- package --------------------------------------------------------------------------------------------------------------
if (Test-Path $OutDir) { Remove-Item -Recurse -Force $OutDir }
New-Item -ItemType Directory -Force (Join-Path $OutDir 'lib') | Out-Null
New-Item -ItemType Directory -Force (Join-Path $OutDir 'include') | Out-Null
Copy-Item -Recurse (Join-Path $gnsSrc 'include/steam') (Join-Path $OutDir 'include/steam')
$gnsLib = Get-ChildItem -Recurse -Filter GameNetworkingSockets_s.lib $gnsBuild | Select-Object -First 1
if (-not $gnsLib) { throw 'GameNetworkingSockets_s.lib was not built' }
Copy-Item $gnsLib.FullName (Join-Path $OutDir 'lib')
Copy-Item (Join-Path $vcpkgInstalled 'lib/libprotobuf.lib') (Join-Path $OutDir 'lib')
Copy-Item (Join-Path $vcpkgInstalled 'lib/absl_*.lib') (Join-Path $OutDir 'lib')
Copy-Item (Join-Path $vcpkgInstalled 'lib/utf8_*.lib') (Join-Path $OutDir 'lib')   # utf8_range (+ utf8_validity in other protobuf versions)
Set-Content -Encoding ascii (Join-Path $OutDir 'ca-pin.txt') $caPin

# the licences of what ends up inside dinput.dll (BSD-3-Clause, Apache-2.0 and MIT all ask for their notice with a binary)
$notices = [System.Collections.Generic.List[string]]::new()
$notices.Add('Third-party software linked into dinput.dll (online play). XML2 Fix itself is MIT licensed: see LICENSE.')
$sources = @(
	@('GameNetworkingSockets (Valve Corporation), BSD-3-Clause', (Join-Path $gnsSrc 'LICENSE')),
	@('vjson (part of GameNetworkingSockets)', (Join-Path $gnsSrc 'src/external/vjson/LICENSE')),
	@('curve25519-donna (part of GameNetworkingSockets)', (Join-Path $gnsSrc 'src/external/curve25519-donna/README.md')),
	@('ed25519-donna (part of GameNetworkingSockets)', (Join-Path $gnsSrc 'src/external/ed25519-donna/README.md')),
	@('protobuf', (Join-Path $vcpkgInstalled 'share/protobuf/copyright')),
	@('abseil', (Join-Path $vcpkgInstalled 'share/abseil/copyright')),
	@('utf8-range', (Join-Path $vcpkgInstalled 'share/utf8-range/copyright'))
)
foreach ($source in $sources) {
	if (-not (Test-Path $source[1])) { throw "licence file missing: $($source[1])" }
	$notices.Add(('=' * 78) + "`r`n" + $source[0] + "`r`n" + ('=' * 78) + "`r`n" + (Get-Content -Raw $source[1]).TrimEnd())
}
Set-Content -Encoding utf8 (Join-Path $OutDir 'THIRD-PARTY-NOTICES.txt') ($notices -join "`r`n`r`n")

$libs = Get-ChildItem (Join-Path $OutDir 'lib') -Filter *.lib | Sort-Object Name
foreach ($need in 'GameNetworkingSockets_s.lib', 'libprotobuf.lib', 'utf8_range.lib') {
	if (-not ($libs | Where-Object Name -eq $need)) { throw "package lacks lib/$need" }
}
if (@($libs | Where-Object Name -like 'absl_*').Count -lt 50) { throw 'package has too few abseil libs' }
$manifest = @(
	'GameNetworkingSockets package for xml2-fix (scripts/build-gns.ps1)'
	"GameNetworkingSockets $GnsTag $GnsCommit + packaging/gns/gns-p2p-require-signed-cert.patch, vjson $VjsonCommit"
	"vcpkg $VcpkgCommit, triplet $Triplet (host $HostTriplet)"
	($ExpectedPorts.Keys | ForEach-Object { "port $_ $($ExpectedPorts[$_])" })
	"toolchain $generator, MSVC $toolsetVersion"
	"CA pin $caPin"
	''
	'SHA-256 of the libraries:'
	($libs | ForEach-Object { '{0} *lib/{1}' -f (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant(), $_.Name })
)
Set-Content -Encoding ascii (Join-Path $OutDir 'gns-package.txt') $manifest
Write-Host "GNS package ready: $OutDir ($($libs.Count) libraries, MSVC $toolsetVersion)"
