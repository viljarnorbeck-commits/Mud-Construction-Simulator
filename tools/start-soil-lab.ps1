[CmdletBinding()]
param(
    [ValidateSet('Play', 'Validate', 'Benchmark')]
    [string]$Mode = 'Play',
    [string]$EngineRoot = 'C:/Program Files/Epic Games/UE_5.7'
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$project = Join-Path $projectRoot 'MudConstructionSimulator.uproject'
$editorName = if ($Mode -eq 'Validate') { 'UnrealEditor-Cmd.exe' } else { 'UnrealEditor.exe' }
$editor = Join-Path $EngineRoot "Engine/Binaries/Win64/$editorName"
if (-not (Test-Path -LiteralPath $editor -PathType Leaf)) {
    throw "Unreal Engine 5.7 is required. Set -EngineRoot. Missing $editor"
}
if (-not (Test-Path -LiteralPath (Join-Path $projectRoot 'Binaries/Win64/UnrealEditor-MudConstructionSimulator.dll'))) {
    throw 'Build the Editor target first with tools/build-windows.ps1 -Target Editor.'
}
$results = Join-Path $projectRoot ('Saved/SoilTests/' + [Guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $results -Force)
$log = Join-Path $results 'SoilLab.log'
$arguments = @(('"{0}"' -f $project), '/Game/Generated/Valley', '-game', '-MudSoilLab', ('-abslog="{0}"' -f $log))
if ($Mode -eq 'Validate') {
    $report = Join-Path $results 'Acceptance.txt'
    $arguments += @('-NullRHI', '-nosound', '-unattended', '-MudSoilValidation', ('-MudSmokeReport="{0}"' -f $report))
    $process = Start-Process -FilePath $editor -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit(240000)) {
        $process.Kill()
        throw "Acceptance timed out. Inspect $log"
    }
    if ($process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $report) -or
        (Get-Content -LiteralPath $report -Raw).Trim() -ne 'passed') {
        throw "Soil acceptance did not pass (exit $($process.ExitCode)). Inspect $log"
    }
    Write-Output "Soil acceptance passed. Log: $log"
} else {
    $arguments += @('-windowed', '-ResX=1920', '-ResY=1080')
    if ($Mode -eq 'Benchmark') { $arguments += @('-MudBenchmark', '-MudBenchmarkExit', '-NoVSync') }
    Start-Process -FilePath $editor -ArgumentList $arguments -WorkingDirectory $projectRoot
    Write-Output "Soil lab requested. Log: $log"
    if ($Mode -eq 'Benchmark') { Write-Output 'The 33-second rendering benchmark writes Saved/Benchmark.csv.' }
}
