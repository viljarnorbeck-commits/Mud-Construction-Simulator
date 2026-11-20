param([Parameter(Mandatory)][string]$Output,[string]$EngineRoot='C:/Program Files/Epic Games/UE_5.7')
$ErrorActionPreference='Stop'
$root=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
if(-not[IO.Path]::IsPathRooted($Output)){$Output=Join-Path $root $Output}
if(Test-Path -LiteralPath $Output){throw 'Choose a fresh output folder.'}
[void](New-Item -ItemType Directory -Path $Output)
$args=@(('"{0}"' -f (Join-Path $root 'MudConstructionSimulator.uproject')),'/Game/Generated/Valley','-game','-NoLiveCoding','-NoTraceServer','-NoSplash','-unattended','-windowed','-ResX=1600','-ResY=900','-MudSoilLab','-MudSoftSoilReview',('-MudMaterialReview="{0}"' -f $Output),('-abslog="{0}"' -f (Join-Path $Output 'Review.log')),'-ExecCmds="t.MaxFPS 90,r.ScreenPercentage 100,viewmode lit"')
$process=Start-Process -FilePath (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor.exe') -ArgumentList $args -WindowStyle Normal -PassThru
if(-not $process.WaitForExit(300000)){$process.Kill();throw 'Soft-soil graphical review timed out.'}
if($process.ExitCode -ne 0 -or -not(Test-Path -LiteralPath (Join-Path $Output 'Result.txt')) -or (Get-Content -LiteralPath (Join-Path $Output 'Result.txt') -Raw).Trim() -ne 'passed'){throw "Soft-soil review failed: $Output"}
foreach($name in @('soil-steep-before','soil-slump','blade-push','blade-release','loader-carry','loader-tip')){
 if(-not(Test-Path -LiteralPath (Join-Path $Output ($name+'.png')))){throw "Missing screenshot: $name"}
}
Get-Content -LiteralPath (Join-Path $Output 'soft-soil.csv')
Write-Output "Soft-soil graphical review passed: $Output"
