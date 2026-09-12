# Batch 21 (G7): one-shot deployment script.
#   .\scripts\deploy.ps1                       -> deploy/ folder
#   .\scripts\deploy.ps1 -BuildDir build -OutDir deploy -Zip
# Flow: build -> cmake --install --prefix <OutDir> -> sanity checks
#       -> optional NexusAI-<version>-win64.zip
# Prerequisites: an already-configured build directory (cmake -B build -G Ninja)
# and MinGW toolchain in PATH for the resource compiler.
param(
    [string]$BuildDir = "build",
    [string]$OutDir = "deploy",
    [switch]$Zip
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

if (-not (Test-Path (Join-Path $BuildDir "CMakeCache.txt"))) {
    throw "Build directory '$BuildDir' has no CMakeCache.txt - configure first: cmake -B $BuildDir -G Ninja"
}

# Native tools (cmake) print warnings on stderr; with $ErrorActionPreference
# = Stop PowerShell 5.1 escalates those into terminating errors - judge by
# exit code instead.
function Run([scriptblock]$cmd) {
    $prev = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    & $cmd
    $code = $LASTEXITCODE
    $ErrorActionPreference = $prev
    if ($code -ne 0) { throw "command failed (exit ${code}): $cmd" }
}

Write-Host "== Building ==" -ForegroundColor Cyan
Run { cmake --build $BuildDir }

# Qt's generated deploy script requires an ABSOLUTE install prefix
# (qt6_deploy_qt_conf writes bin/qt.conf paths into it).
if (-not [System.IO.Path]::IsPathRooted($OutDir)) {
    $OutDir = Join-Path $repoRoot $OutDir
}

Write-Host "== Installing to $OutDir ==" -ForegroundColor Cyan
Run { cmake --install $BuildDir --prefix $OutDir }

Write-Host "== Sanity checks ==" -ForegroundColor Cyan
# Qt deploy layout: exe + Qt DLLs + qt.conf all live in <OutDir>/bin
$exe = Join-Path $OutDir "bin\nexus_ai.exe"
if (-not (Test-Path $exe)) { throw "bin\nexus_ai.exe missing in $OutDir" }
foreach ($dll in @("Qt6Core.dll", "Qt6Gui.dll", "Qt6Qml.dll", "Qt6Quick.dll")) {
    if (-not (Test-Path (Join-Path $OutDir "bin\$dll"))) {
        throw "bin\$dll missing in $OutDir - deploy script did not collect Qt runtime"
    }
}
$platformDll = Get-ChildItem $OutDir -Recurse -Filter "qwindows*.dll" | Select-Object -First 1
if (-not $platformDll) { throw "platform plugin qwindows*.dll missing - GUI cannot start" }
Write-Host ("Deployed OK: {0} files" -f (Get-ChildItem $OutDir -Recurse -File).Count) -ForegroundColor Green

if ($Zip) {
    $version = "0.0.0"
    $projLine = Select-String -Path (Join-Path $repoRoot "CMakeLists.txt") -Pattern "^project\(\s*NexusAI\s+VERSION\s+([0-9.]+)"
    if ($projLine) { $version = $projLine.Matches[0].Groups[1].Value }
    $zipPath = Join-Path $repoRoot ("NexusAI-{0}-win64.zip" -f $version)
    if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
    Compress-Archive -Path (Join-Path $OutDir "*") -DestinationPath $zipPath
    Write-Host "Archive: $zipPath" -ForegroundColor Green
}
