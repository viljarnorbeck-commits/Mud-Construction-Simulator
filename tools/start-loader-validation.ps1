param([string]$EngineRoot = 'C:/Program Files/Epic Games/UE_5.7')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$results = Join-Path $root ('Saved/LoaderTests/' + [Guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Force -Path $results)
$log = Join-Path $results 'Loader.log'
$report = Join-Path $results 'Result.txt'
$editor = Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$arguments = @(('"{0}"' -f (Join-Path $root 'MudConstructionSimulator.uproject')),
    '/Game/Generated/Valley', '-game', '-NoLiveCoding', '-NullRHI', '-nosound', '-unattended',
    '-MudLoaderValidation', ('-abslog="{0}"' -f $log), ('-MudSmokeReport="{0}"' -f $report))
$process = Start-Process -FilePath $editor -ArgumentList $arguments -WindowStyle Hidden -PassThru
if (-not $process.WaitForExit(240000)) { $process.Kill(); throw "Loader acceptance timed out. $log" }
if ($process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $report) -or
    (Get-Content -LiteralPath $report -Raw).Trim() -ne 'passed') {
    throw "Loader acceptance failed (exit $($process.ExitCode)). $log"
}
Write-Output "Loader acceptance passed. $log"
