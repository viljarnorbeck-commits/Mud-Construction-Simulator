[CmdletBinding()]
param([Parameter(Mandatory)][string]$Output,[string]$EngineRoot='C:/Program Files/Epic Games/UE_5.7',[switch]$Audit)
$ErrorActionPreference='Stop'
$root=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
if(-not[IO.Path]::IsPathRooted($Output)){$Output=Join-Path $root $Output}
if(Test-Path -LiteralPath $Output){throw 'Choose a new output directory; stale captures are never accepted.'}
[void](New-Item -ItemType Directory -Path $Output)
$log=Join-Path $Output 'Review.log'
$report=Join-Path $Output 'Result.txt'
$exe=Join-Path $EngineRoot ('Engine/Binaries/Win64/'+$(if($Audit){'UnrealEditor-Cmd.exe'}else{'UnrealEditor.exe'}))
$args=@(('"{0}"' -f (Join-Path $root 'MudConstructionSimulator.uproject')),'/Game/Generated/Valley','-game','-NoLiveCoding','-NoTraceServer','-NoSplash','-unattended',('-abslog="{0}"' -f $log))
if($Audit){$args+=@('-MudTerrainAudit','-NullRHI','-nosound',('-MudSmokeReport="{0}"' -f $report))}
else{$args+=@('-windowed','-ResX=1600','-ResY=900','-MudTerrainReview',('-MudMaterialReview="{0}"' -f $Output),'-ExecCmds="t.MaxFPS 90,r.ScreenPercentage 100"')}
$started=[DateTime]::UtcNow
$process=Start-Process -FilePath $exe -ArgumentList $args -WindowStyle $(if($Audit){'Hidden'}else{'Normal'}) -PassThru
if(-not$process.WaitForExit(240000)){$process.Kill();throw "Terrain review timed out: $log"}
if($process.ExitCode -ne 0){throw "Terrain review failed: $log"}
if($Audit){
    if(-not(Test-Path -LiteralPath $report) -or (Get-Content -LiteralPath $report -Raw).Trim() -ne 'passed'){throw 'Native terrain audit failed.'}
}else{
    foreach($name in @('yard-slope-low','ravine-low','terrace-aerial','western-ridges','tributary-valley','eastern-rock')){
        $image=Join-Path $Output ($name+'.png')
        if(-not(Test-Path -LiteralPath $image) -or (Get-Item -LiteralPath $image).LastWriteTimeUtc -lt $started){throw "Missing/stale capture: $name"}
    }
    foreach($row in Import-Csv -LiteralPath (Join-Path $Output 'drive.csv')){
        if([double]$row.travel_m -lt .5){throw "Production driving did not progress: $($row.stage)"}
    }
}
Write-Output "Terrain review passed: $Output"
