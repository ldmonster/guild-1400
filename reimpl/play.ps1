<#
.SYNOPSIS
  Rebuild the Die-Gilde reimplementation with the real Vulkan+SDL2 backend and
  launch the playable city loop (guild_run --play).

.DESCRIPTION
  This project is GCC-first (its CMakeLists uses -Wall/-Wextra, which MSVC rejects),
  so we build with TDM-GCC / MinGW. The Vulkan + SDL2 backend deps come from vcpkg
  (sdl2, vulkan-headers, vulkan-loader from vcpkg.json), built as x64-mingw-dynamic
  so they link with the MinGW-built engine.

  Two portability fixes discovered while bringing the build up on Windows are baked
  in (neither is a code change):
    * -D_USE_MATH_DEFINES         : MinGW hides M_PI under strict -std=c++17.
    * -Wl,--allow-multiple-definition : a pre-existing duplicate guild::sim::CutsceneRng
                                        (combat.h vs cutscene.h) collides at link; the
                                        two defs are identical, so first-wins is safe.

  Requirements (the script checks/sets up what it can):
    * TDM-GCC / MinGW       (expected at C:\TDM-GCC-64; override with -MinGwBin)
    * CMake 3.21+           (on PATH)
    * git                   (to fetch vcpkg if missing)
    * A Vulkan-capable GPU driver (to actually present a window). Headless falls back
      to offscreen and needs --frames to terminate (use -Frames).

.PARAMETER GameDir
  A real "Die Gilde" install (defaults to the bundled europe_guild_1400_original).

.PARAMETER Clean
  Wipe the build-vk directory and reconfigure from scratch.

.PARAMETER Frames
  If > 0, pass --frames N (bounded run; required for a headless/no-display run).
  0 (default) = play until ESC / window close.

.PARAMETER ConfigureOnly
  Configure + build only; do not launch.

.EXAMPLE
  .\play.ps1                      # rebuild + play (windowed)
  .\play.ps1 -Clean               # clean rebuild + play
  .\play.ps1 -Frames 240          # bounded run (headless-safe)
#>
[CmdletBinding()]
param(
    [string]$GameDir = "",
    [switch]$Clean,
    [int]$Frames = 0,
    [switch]$ConfigureOnly,
    [string]$MinGwBin = "C:\TDM-GCC-64\bin",
    [string]$Triplet = "x64-mingw-dynamic"
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir = Join-Path $RepoRoot "build-vk"
if (-not $GameDir) { $GameDir = Join-Path $RepoRoot "europe_guild_1400_original" }

function Step($msg) { Write-Host "`n==> $msg" -ForegroundColor Cyan }
function Die($msg)  { Write-Host "ERROR: $msg" -ForegroundColor Red; exit 1 }

# --- 1. Toolchain on PATH ---------------------------------------------------
Step "Toolchain"
$gpp = Join-Path $MinGwBin "g++.exe"
if (-not (Test-Path $gpp)) { Die "g++ not found at $gpp. Install TDM-GCC or pass -MinGwBin." }
if (($env:Path -split ';') -notcontains $MinGwBin) { $env:Path = "$MinGwBin;$env:Path" }
& (Join-Path $MinGwBin "g++.exe") --version | Select-Object -First 1
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) { Die "cmake not on PATH." }

# --- 2. vcpkg (clone + bootstrap if needed) ---------------------------------
Step "vcpkg"
$vcpkgRoot = $env:VCPKG_ROOT
if (-not $vcpkgRoot -or -not (Test-Path (Join-Path $vcpkgRoot "scripts\buildsystems\vcpkg.cmake"))) {
    $vcpkgRoot = Join-Path $env:USERPROFILE "vcpkg"
    if (-not (Test-Path (Join-Path $vcpkgRoot ".git"))) {
        if (-not (Get-Command git -ErrorAction SilentlyContinue)) { Die "git not on PATH (needed to fetch vcpkg)." }
        Write-Host "Cloning vcpkg into $vcpkgRoot ..."
        git clone https://github.com/microsoft/vcpkg $vcpkgRoot
    }
    if (-not (Test-Path (Join-Path $vcpkgRoot "vcpkg.exe"))) {
        Write-Host "Bootstrapping vcpkg ..."
        & (Join-Path $vcpkgRoot "bootstrap-vcpkg.bat") -disableMetrics
    }
    $env:VCPKG_ROOT = $vcpkgRoot
}
$toolchain = Join-Path $vcpkgRoot "scripts\buildsystems\vcpkg.cmake"
Write-Host "VCPKG_ROOT = $vcpkgRoot"
Write-Host "Target triplet = $Triplet (deps: sdl2, vulkan-headers, vulkan-loader)"

# The manifest pins a builtin-baseline commit. vcpkg reads BOTH versions/baseline.json
# AND the versions DB from the clone, so the clone must be checked out AT that commit
# with full history (a fresh master may be older/newer than the pinned baseline and
# lack the referenced port versions). Pin the clone to the baseline.
$manifest = Join-Path $RepoRoot "vcpkg.json"
if (Test-Path $manifest) {
    $baseline = (Get-Content -Raw $manifest | ConvertFrom-Json)."builtin-baseline"
    if ($baseline) {
        $head = (& git -C $vcpkgRoot rev-parse HEAD).Trim()
        if ($head -ne $baseline) {
            Write-Host "Pinning vcpkg clone to baseline $baseline ..."
            & git -C $vcpkgRoot cat-file -t $baseline *>$null
            if ($LASTEXITCODE -ne 0) {
                & git -C $vcpkgRoot fetch --unshallow 2>$null
                & git -C $vcpkgRoot fetch origin
            }
            & git -C $vcpkgRoot checkout --quiet $baseline
            if ($LASTEXITCODE -ne 0) { Die "Could not check out vcpkg baseline $baseline." }
        }
    }
}

# --- 3. Configure -----------------------------------------------------------
Step "Configure (build-vk)"
if ($Clean -and (Test-Path $BuildDir)) { Remove-Item -Recurse -Force $BuildDir }
$cfgArgs = @(
    "-S", $RepoRoot, "-B", $BuildDir,
    "-G", "MinGW Makefiles",
    "-DCMAKE_BUILD_TYPE=Debug",
    "-DGUILD_BACKEND=ON",
    "-DCMAKE_C_COMPILER=$(Join-Path $MinGwBin 'gcc.exe')",
    "-DCMAKE_CXX_COMPILER=$gpp",
    "-DCMAKE_MAKE_PROGRAM=$(Join-Path $MinGwBin 'mingw32-make.exe')",
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
    "-DVCPKG_TARGET_TRIPLET=$Triplet",
    "-DVCPKG_HOST_TRIPLET=$Triplet",
    "-DCMAKE_CXX_FLAGS=-D_USE_MATH_DEFINES",
    "-DCMAKE_EXE_LINKER_FLAGS=-Wl,--allow-multiple-definition"
)
cmake @cfgArgs
if ($LASTEXITCODE -ne 0) { Die "CMake configure failed (vcpkg may still be building SDL2/Vulkan on first run)." }

# --- 4. Build guild_run -----------------------------------------------------
Step "Build guild_run"
cmake --build $BuildDir --target guild_run -j ([Environment]::ProcessorCount)
if ($LASTEXITCODE -ne 0) { Die "Build failed." }

$exe = Get-ChildItem -Path $BuildDir -Recurse -Filter "guild_run.exe" | Select-Object -First 1
if (-not $exe) { Die "guild_run.exe not produced." }
Write-Host "Built: $($exe.FullName)" -ForegroundColor Green
if ($ConfigureOnly) { exit 0 }

# --- 5. Play ----------------------------------------------------------------
Step "Play"
if (-not (Test-Path (Join-Path $GameDir "gfx\gilde.gfx"))) {
    Die "GameDir '$GameDir' has no gfx\gilde.gfx -- point -GameDir at a real Die Gilde install."
}
# Match the original's configured resolution (Gilde.INI screen_x/screen_y) so the
# menu/GUI lays out identically — the engine draws the GUI at native size and only
# center-translates, so resolution is what makes the menu look "the same".
$ini = Join-Path $GameDir "Gilde.INI"
if (Test-Path $ini) {
    $txt = Get-Content $ini
    $sx = ($txt | Select-String -Pattern '^\s*screen_x\s*=\s*(\d+)' | Select-Object -First 1).Matches.Groups[1].Value
    $sy = ($txt | Select-String -Pattern '^\s*screen_y\s*=\s*(\d+)' | Select-Object -First 1).Matches.Groups[1].Value
    if ($sx -and $sy) {
        $env:GUILD_PLAY_W = $sx; $env:GUILD_PLAY_H = $sy
        Write-Host "Matching original resolution from Gilde.INI: ${sx}x${sy}" -ForegroundColor Green
    }
}
$runArgs = @("--play", "--game-dir", $GameDir)
if ($Frames -gt 0) { $runArgs += @("--frames", "$Frames") }
Write-Host "> $($exe.FullName) $($runArgs -join ' ')" -ForegroundColor Yellow
& $exe.FullName @runArgs
exit $LASTEXITCODE
