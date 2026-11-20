param([string]$Output='Saved/MaterialReview', [string]$EngineRoot='C:/Program Files/Epic Games/UE_5.7',
    [ValidateRange(640,7680)][int]$Width=1920, [ValidateRange(360,4320)][int]$Height=1080, [switch]$Offscreen,
    [switch]$Dozer,[string]$Project='MudConstructionSimulator.uproject')
$ErrorActionPreference='Stop'
$root=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
if (-not [IO.Path]::IsPathRooted($Output)) { $Output=Join-Path $root $Output }
[void](New-Item -ItemType Directory -Force -Path $Output)
$editor=Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor.exe'
if(-not [IO.Path]::IsPathRooted($Project)){$Project=Join-Path $root $Project}
$arguments=@(('"{0}"' -f $Project),
    '/Game/Generated/Valley','-game','-NoLiveCoding','-NoTraceServer','-NoSplash','-unattended','-windowed',
    ('-ResX='+$Width),('-ResY='+$Height),('-MudMaterialReview="{0}"' -f $Output),('-abslog="{0}"' -f (Join-Path $Output 'Render.log')))
if ($Offscreen) { $arguments+=@('-RenderOffscreen','-ForceRes') }
if ($Dozer) { $arguments+='-MudDozerReview' }
$style=if($Offscreen){'Hidden'}else{'Normal'}
$started=[DateTime]::UtcNow
$process=Start-Process -FilePath $editor -ArgumentList $arguments -WindowStyle $style -PassThru
if (-not $process.WaitForExit(240000)) { $process.Kill(); throw "Rendering timed out. $Output" }
if ($process.ExitCode -ne 0) { throw "Rendering failed (exit $($process.ExitCode)). $Output" }
foreach($file in @('near.png','overview.png','work.png','timings.csv')) {
    if (-not (Test-Path -LiteralPath (Join-Path $Output $file))) {throw "Missing render output: $Output/$file"}
    if ((Get-Item -LiteralPath (Join-Path $Output $file)).LastWriteTimeUtc -lt $started.AddSeconds(-1)) {throw "Stale render output: $Output/$file"}
}
Write-Output "Material views rendered: $Output"
