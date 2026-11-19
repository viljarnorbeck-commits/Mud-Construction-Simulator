[CmdletBinding()]
param(
    [string]$EngineRoot = 'C:/Program Files/Epic Games/UE_5.7',
    [ValidateSet('Editor', 'Smoke', 'Game', 'Installer')]
    [string]$Target = 'Editor',
    [string]$Inno = 'C:/Program Files (x86)/Inno Setup 6/ISCC.exe',
    [string]$SignTool,
    [ValidatePattern('^[0-9a-fA-F]{40}$')]
    [string]$CertificateThumbprint,
    [ValidatePattern('^https://[^\s"$]+$')]
    [string]$TimestampUrl,
    [string]$WorldDirectory = 'world/highlands-42',
    [switch]$Launch
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$project = Join-Path $projectRoot 'MudConstructionSimulator.uproject'
$build = Join-Path $EngineRoot 'Engine/Build/BatchFiles/Build.bat'
$editor = Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$uat = Join-Path $EngineRoot 'Engine/Build/BatchFiles/RunUAT.bat'

if (-not (Test-Path -LiteralPath $editor -PathType Leaf) -and -not $PSBoundParameters.ContainsKey('EngineRoot')) {
    $launcherManifest = Join-Path $env:ProgramData 'Epic/UnrealEngineLauncher/LauncherInstalled.dat'
    if (Test-Path -LiteralPath $launcherManifest -PathType Leaf) {
        $installation = (Get-Content -LiteralPath $launcherManifest -Raw | ConvertFrom-Json).InstallationList |
            Where-Object AppName -eq 'UE_5.7' | Select-Object -First 1
        if ($installation.InstallLocation) {
            $EngineRoot = $installation.InstallLocation
            $build = Join-Path $EngineRoot 'Engine/Build/BatchFiles/Build.bat'
            $editor = Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
            $uat = Join-Path $EngineRoot 'Engine/Build/BatchFiles/RunUAT.bat'
        }
    }
}

foreach ($file in @($editor, $build, $uat)) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
        throw "Unreal Engine 5.7 is required. Set -EngineRoot to the installation folder. Missing $file"
    }
}

function Invoke-BuildStep {
    param([string]$Executable, [string[]]$Arguments)
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Executable exited with code $LASTEXITCODE."
    }
}

$world = Join-Path $projectRoot 'Content/World'
[void](New-Item -ItemType Directory -Force -Path $world)
foreach ($file in 'height.png', 'overview.png', 'world.json') {
    Copy-Item -LiteralPath (Join-Path (Join-Path $projectRoot $WorldDirectory) $file) -Destination $world -Force
}

Invoke-BuildStep -Executable $build -Arguments @(
    'MudConstructionSimulatorEditor', 'Win64', 'Development', "-Project=$project", '-WaitMutex'
)
$bootstrap = Join-Path $PSScriptRoot 'bootstrap-unreal.py'
Invoke-BuildStep -Executable $editor -Arguments @(
    $project, "-ExecutePythonScript=$bootstrap", '-unattended', '-nop4', '-NullRHI', '-stdout', '-FullStdOutLogOutput'
)
if (-not (Test-Path -LiteralPath (Join-Path $projectRoot 'Content/Generated/Valley.umap') -PathType Leaf)) {
    throw "Unreal did not generate the Valley map. See the Unreal script log."
}

if ($Target -eq 'Editor') {
    if ($Launch) {
        $editorGui = Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor.exe'
        Start-Process -FilePath $editorGui -ArgumentList ('"{0}"' -f $project)
    }
    return
}

if ($Target -eq 'Smoke') {
    Invoke-BuildStep -Executable $editor -Arguments @(
        $project, '/Game/Generated/Valley', '-game', '-NullRHI', '-nosound', '-unattended',
        '-MudSmokeTest', '-stdout', '-FullStdOutLogOutput'
    )
    return
}

$config = Join-Path $projectRoot 'Config/DefaultGame.ini'
$match = Select-String -LiteralPath $config -Pattern '^ProjectVersion=([0-9]{1,5}\.[0-9]{1,5}\.[0-9]{1,5})\s*$'
if (@($match).Count -ne 1 -or [version]($match.Matches[0].Groups[1].Value) -gt [version]'65535.65535.65535') {
    throw "Set ProjectVersion in $config to a valid three-part Windows version."
}
$version = [version]($match.Matches[0].Groups[1].Value)
if ($version.Minor -gt 65535 -or $version.Build -gt 65535) {
    throw 'Each version component must be at most 65535.'
}
$dist = Join-Path $projectRoot "Dist/$($version)-$([Guid]::NewGuid().ToString('N').Substring(0, 10))"
$appLocal = Join-Path $EngineRoot 'Engine/Binaries/ThirdParty/AppLocalDependencies'
$cookArguments = @(
    'BuildCookRun', "-project=$project", '-platform=Win64', '-clientconfig=Shipping',
    '-build', '-cook', '-stage', '-pak', '-iostore', '-package', '-archive',
    "-archivedirectory=$dist", '-unattended', '-nop4'
)
if (Test-Path -LiteralPath (Join-Path $appLocal 'Win64') -PathType Container) {
    $cookArguments += "-applocaldirectory=$appLocal"
}
Invoke-BuildStep -Executable $uat -Arguments $cookArguments
$windows = Join-Path $dist 'Windows'
$game = Join-Path $windows 'MudConstructionSimulator.exe'
if (-not (Test-Path -LiteralPath $game -PathType Leaf)) {
    throw "Expected the packaged game at $game."
}
$win64 = Join-Path $windows 'MudConstructionSimulator/Binaries/Win64'
$requiredRuntime = @('msvcp140.dll','vcruntime140.dll','vcruntime140_1.dll')
$missingRuntime = $requiredRuntime | Where-Object { -not (Test-Path -LiteralPath (Join-Path $win64 $_) -PathType Leaf) }
if ($missingRuntime) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
        throw 'Missing app-local Visual C++ DLLs and Visual Studio Installer is unavailable.'
    }
    $vs = & $vswhere -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw 'Visual Studio 2022 C++ redistributable files are required.' }
    $redist = Get-ChildItem -LiteralPath (Join-Path $vs 'VC/Redist/MSVC') -Directory |
        Where-Object Name -Match '^14\.\d+\.\d+$' | Sort-Object { [version]$_.Name } -Descending |
        ForEach-Object { Join-Path $_.FullName 'x64/Microsoft.VC143.CRT' } |
        Where-Object { Test-Path -LiteralPath $_ -PathType Container } | Select-Object -First 1
    if (-not $redist) { throw 'The Visual Studio installation lacks the x64 VC143 redistributable folder.' }
    Get-ChildItem -LiteralPath $redist -Filter '*.dll' -File | Copy-Item -Destination $win64 -Force
    foreach ($dll in $requiredRuntime) {
        if (-not (Test-Path -LiteralPath (Join-Path $win64 $dll) -PathType Leaf)) {
            throw "Could not stage the app-local runtime DLL $dll."
        }
    }
}
Write-Output "Game: $game"
if ($Target -eq 'Installer') {
    $packageArgs = @{ GameFiles = $windows; OutputDirectory = $dist; Version = $version }
    foreach ($option in 'Inno','SignTool','CertificateThumbprint','TimestampUrl') {
        if ($PSBoundParameters.ContainsKey($option)) {
            $packageArgs[$option] = $PSBoundParameters[$option]
        }
    }
    & (Join-Path $PSScriptRoot 'package-windows-installer.ps1') @packageArgs
}
if ($Launch) {
    Start-Process -FilePath $game -WorkingDirectory $windows
}
