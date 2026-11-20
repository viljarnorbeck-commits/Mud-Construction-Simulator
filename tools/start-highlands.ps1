[CmdletBinding()]
param(
    [ValidateSet('Play','Validate','Regression','Benchmark')][string]$Mode='Play',
    [string]$EngineRoot='C:/Program Files/Epic Games/UE_5.7',
    [ValidateSet('42','earthworks-42','highlands-42')][string]$World='42'
)
$ErrorActionPreference='Stop'
$root=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$source=Join-Path $root ('world/'+$World)
$content=Join-Path $root 'Content/World'
[void](New-Item -ItemType Directory -Force -Path $content)
foreach($file in @('height.png','overview.png','world.json')) {
    Copy-Item -LiteralPath (Join-Path $source $file) -Destination $content -Force
}
$isTest=$Mode -in @('Validate','Regression')
$name=if($isTest){'UnrealEditor-Cmd.exe'}else{'UnrealEditor.exe'}
$editor=Join-Path $EngineRoot "Engine/Binaries/Win64/$name"
if(-not(Test-Path -LiteralPath $editor)){throw 'Unreal Engine 5.7 is required. Set -EngineRoot.'}
if(-not(Test-Path -LiteralPath (Join-Path $root 'Binaries/Win64/UnrealEditor-MudConstructionSimulator.dll'))){
    throw 'Build first with tools/build-windows.ps1 -Target Editor.'
}
$results=Join-Path $root ('Saved/HighlandsTests/'+[Guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Force -Path $results)
$log=Join-Path $results 'Highlands.log'
$arguments=@(('"{0}"' -f (Join-Path $root 'MudConstructionSimulator.uproject')),
    '/Game/Generated/Valley','-game','-NoLiveCoding','-NoTraceServer',('-abslog="{0}"' -f $log))
if($isTest) {
    $report=Join-Path $results 'Result.txt'
    $flag=if($Mode -eq 'Validate'){'-MudWorldValidation'}else{'-MudSmokeTest'}
    $arguments+=@($flag,'-NullRHI','-nosound','-unattended',('-MudSmokeReport="{0}"' -f $report))
    $process=Start-Process -FilePath $editor -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if(-not $process.WaitForExit(120000)){$process.Kill();throw "Validation timed out. $log"}
    if($process.ExitCode -ne 0 -or -not(Test-Path -LiteralPath $report) -or (Get-Content -LiteralPath $report -Raw).Trim() -ne 'passed'){
        throw "Highlands validation did not pass (exit $($process.ExitCode)). $log"
    }
    Write-Output "Highlands validation passed. $log"
}else{
    $arguments+=@('-windowed','-ResX=1920','-ResY=1080')
    if($Mode -eq 'Benchmark'){$arguments+=@('-MudBenchmark','-MudBenchmarkExit','-NoVSync','-ExecCmds="sg.ResolutionQuality 100,r.ScreenPercentage 100,t.MaxFPS 0"')}
    Start-Process -FilePath $editor -ArgumentList $arguments -WorkingDirectory $root
    Write-Output "Highlands launch requested. Log: $log"
}
