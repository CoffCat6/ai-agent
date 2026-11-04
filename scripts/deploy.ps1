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

# B6-2: the deploy script may only write/clean a directory it owns. Default
# -OutDir is <repo>/deploy; a user profile, a drive root, the repo root and
# arbitrary personal folders are refused before -Clean can delete anything.
function Assert-SafeOutDir([string]$Dir) {
    $full = [System.IO.Path]::GetFullPath($Dir).TrimEnd('\')
    $driveRoot = [System.IO.Path]::GetPathRoot($full).TrimEnd('\')
    $repo = [System.IO.Path]::GetFullPath($repoRoot).TrimEnd('\')
    $homeDir = if ($env:USERPROFILE) { [System.IO.Path]::GetFullPath($env:USERPROFILE).TrimEnd('\') } else { "" }

    if ($full -eq "") { throw "deploy: empty output directory" }
    if ($full -ieq $driveRoot) { throw "deploy: refusing to write into a drive root: $full" }
    if ($full -ieq $repo) { throw "deploy: refusing to write into the repository root: $full" }
    if ($homeDir) {
        if ($full -ieq $homeDir) {
            throw "deploy: refusing to write into the user profile: $full"
        }
        if ($full.StartsWith($homeDir + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "deploy: refusing to write into the user profile subtree: $full (choose a dedicated deployment directory outside the profile)"
        }
    }

    # -Clean deletes bin/plugins/qml/translations. Only do that for a
    # directory that is empty or already carries the NexusAI install layout;
    # otherwise it may belong to the user or another application.
    if ($Clean -and (Test-Path $full)) {
        $owned = (Test-Path (Join-Path $full 'bin\nexus_ai.exe')) -or
                 (Test-Path (Join-Path $full 'bin\qt.conf'))
        $firstEntry = Get-ChildItem -LiteralPath $full -Force | Select-Object -First 1
        if (-not $owned -and $null -ne $firstEntry) {
            throw "deploy: refusing to clean '$full': not a NexusAI deploy directory (no bin\nexus_ai.exe or bin\qt.conf) and not empty"
        }
    }
    return $full
}

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

# Qt's generated deploy script requires an ABSOLUTE install prefix
# (qt6_deploy_qt_conf writes bin/qt.conf paths into it). Resolve and validate
# it *before* building, so -Clean can never touch a user directory and a bad
# -OutDir fails fast instead of after a full build.
if (-not [System.IO.Path]::IsPathRooted($OutDir)) {
    $OutDir = Join-Path $repoRoot $OutDir
}
$OutDir = Assert-SafeOutDir $OutDir

Write-Host "== Building ==" -ForegroundColor Cyan
Run { cmake --build $BuildDir }

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
# B6-2: every Qt module the targets link directly must be present in the
# bundle before it is handed to the user / uploaded as an artifact. A stale
# deploy silently loses newly added modules and the app aborts at load time
# with STATUS_DLL_NOT_FOUND. Keep this list in sync with CMakeLists.txt:
#   nexus_core       -> Qt6Core/Gui(transitive)/Sql/Network
#   nexus_infra      -> Qt6Network
#   nexus_viewmodels -> Qt6Quick/Qml/Pdf
#   nexus_ai         -> Qt6Quick/QuickControls2/Widgets
#
# Qt6Concurrent is linked in CMakeLists.txt (nexus_core PUBLIC Qt6::Concurrent)
# but is deliberately NOT required here: in Qt 6.9 QtConcurrent::run and
# QFutureWatcher are header-only over Qt6Core's QThreadPool, and the compiled
# nexus_ai.exe has no Qt6Concurrent.dll entry in its PE import table (verified
# with objdump -p). Requiring it would fail a correct windeployqt bundle.
$requiredQtDlls = @(
    "Qt6Core.dll", "Qt6Gui.dll", "Qt6Qml.dll", "Qt6Quick.dll",
    "Qt6QuickControls2.dll", "Qt6Widgets.dll", "Qt6Sql.dll",
    "Qt6Network.dll", "Qt6Pdf.dll"
)
# Task baseline mentions Qt6Concurrent explicitly; report it but never fail
# when the linker folded all QtConcurrent symbols into Qt6Core.
$compileOnlyQtDlls = @("Qt6Concurrent.dll")
$missingQtDlls = @($requiredQtDlls | Where-Object {
    -not (Test-Path (Join-Path $OutDir "bin\$_"))
})
if ($missingQtDlls.Count -gt 0) {
    Write-Host "ERROR: required Qt runtime DLLs are missing from $OutDir\bin:" -ForegroundColor Red
    foreach ($dll in $missingQtDlls) { Write-Host "  - $dll" -ForegroundColor Red }
    Write-Host "The bundle would fail to start (STATUS_DLL_NOT_FOUND)." -ForegroundColor Red
    Write-Host "Re-run with -Clean, or check the qt_generate_deploy_qml_app_script output." -ForegroundColor Red
    exit 1
}
foreach ($dll in $compileOnlyQtDlls) {
    if (-not (Test-Path (Join-Path $OutDir "bin\$dll"))) {
        Write-Host "  note: $dll not bundled (compile-time only in Qt 6.9; not in exe import table)" -ForegroundColor DarkGray
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
