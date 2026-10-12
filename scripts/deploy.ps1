# Batch 21 (G7): one-shot deployment script.
#   .\scripts\deploy.ps1                       -> deploy/ folder
#   .\scripts\deploy.ps1 -BuildDir build -OutDir deploy -Zip
#   .\scripts\deploy.ps1 -Clean                -> wipe stale bin/plugins/qml/translations first
# Flow: build -> cmake --install --prefix <OutDir> -> sanity checks
#       -> optional NexusAI-<version>-win64.zip
# Prerequisites: an already-configured build directory (cmake -B build -G Ninja)
# and MinGW toolchain in PATH for the resource compiler.
param(
    [string]$BuildDir = "build",
    [string]$OutDir = "deploy",
    [switch]$Zip,
    # docs/25: 先清空部署子目录再安装。旧产物残留（如缺少后加入依赖的
    # Qt6Widgets/Qt6Pdf）会让应用启动即报缺 DLL 或平台插件错误。
    [switch]$Clean
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

# Stale artifacts from an older build (e.g. a Qt dependency added since the
# last deploy) are the usual cause of "application failed to start" - wipe
# the known deploy subdirectories before reinstalling. Best effort: some
# environments gate bulk deletes, in which case we warn and continue - the
# install refreshes the bundle and the sanity checks below still verify it.
if ($Clean) {
    foreach ($sub in @("bin", "plugins", "qml", "translations")) {
        $target = Join-Path $OutDir $sub
        if (Test-Path $target) {
            Write-Host "  cleaning stale $sub\" -ForegroundColor DarkGray
            try {
                Remove-Item $target -Recurse -Force -ErrorAction Stop
            } catch {
                Write-Warning "  could not clean $sub\ ($($_.Exception.Message.Trim())) - continuing"
            }
        }
    }
}

Write-Host "== Installing to $OutDir ==" -ForegroundColor Cyan
Run { cmake --install $BuildDir --prefix $OutDir }

Write-Host "== Sanity checks ==" -ForegroundColor Cyan
# Qt deploy layout: exe + Qt DLLs + qt.conf all live in <OutDir>/bin.
$exe = Join-Path $OutDir "bin\nexus_ai.exe"
if (-not (Test-Path $exe)) { throw "bin\nexus_ai.exe missing in $OutDir" }
# Direct Qt runtime deps linked by the exe: Qt6Widgets (tray icon) and
# Qt6Pdf (PDF import) were both added after the initial deploy, so a stale
# bundle silently loses them and aborts at load time.
foreach ($dll in @("Qt6Core.dll", "Qt6Gui.dll", "Qt6Qml.dll", "Qt6Quick.dll",
                   "Qt6Widgets.dll", "Qt6Pdf.dll")) {
    if (-not (Test-Path (Join-Path $OutDir "bin\$dll"))) {
        throw "bin\$dll missing in $OutDir - deploy script did not collect Qt runtime"
    }
}
# qt.conf (Prefix=..) pins Qt's plugin search to <OutDir>/plugins, so the
# platform plugin must be exactly there: otherwise the app aborts with
# "no Qt platform plugin could be initialized / Available ...: windows".
$qtConf = Join-Path $OutDir "bin\qt.conf"
if (-not (Test-Path $qtConf)) { throw "bin\qt.conf missing in $OutDir - plugin paths are not pinned" }
$platformDll = Join-Path $OutDir "plugins\platforms\qwindows.dll"
if (-not (Test-Path $platformDll)) {
    throw "plugins\platforms\qwindows.dll missing in $OutDir - GUI cannot start"
}
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
