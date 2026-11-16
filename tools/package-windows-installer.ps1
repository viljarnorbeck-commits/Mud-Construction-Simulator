[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$GameFiles,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [Parameter(Mandatory)][version]$Version,
    [string]$Inno = 'C:/Program Files (x86)/Inno Setup 6/ISCC.exe',
    [string]$SignTool,
    [ValidatePattern('^[0-9a-fA-F]{40}$')][string]$CertificateThumbprint,
    [ValidatePattern('^https://[^\s"$]+$')][string]$TimestampUrl
)

$ErrorActionPreference = 'Stop'
if ($Version.Build -lt 0 -or $Version.Revision -ne -1 -or
    $Version.Major -gt 65535 -or $Version.Minor -gt 65535 -or $Version.Build -gt 65535) {
    throw 'Version must contain three components from 0 through 65535.'
}
$gameRoot = (Resolve-Path -LiteralPath $GameFiles).Path
$boot = Join-Path $gameRoot 'MudConstructionSimulator.exe'
$binaryFolder = Join-Path $gameRoot 'MudConstructionSimulator/Binaries/Win64'
$shipping = Join-Path $binaryFolder 'MudConstructionSimulator-Win64-Shipping.exe'
foreach ($required in @($boot, $shipping, (Join-Path $binaryFolder 'vcruntime140.dll'),
    (Join-Path $binaryFolder 'vcruntime140_1.dll'), (Join-Path $binaryFolder 'msvcp140.dll'),
    (Join-Path $gameRoot 'MudConstructionSimulator/Content/World/world.json'),
    (Join-Path $gameRoot 'MudConstructionSimulator/Content/World/height.png'),
    (Join-Path $gameRoot 'MudConstructionSimulator/Content/World/overview.png'))) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Incomplete Windows package. Missing $required"
    }
}
$paks = Join-Path $gameRoot 'MudConstructionSimulator/Content/Paks'
foreach ($pattern in '*.pak','*.utoc','*.ucas') {
    if (-not (Get-ChildItem -LiteralPath $paks -Filter $pattern -File | Select-Object -First 1)) {
        throw "Incomplete cooked content. Missing $pattern in $paks"
    }
}
$reparse = Get-ChildItem -LiteralPath $gameRoot -Recurse -Force -Attributes ReparsePoint | Select-Object -First 1
if ($reparse) { throw "The package contains a link or junction: $($reparse.FullName)" }
if (-not $PSBoundParameters.ContainsKey('Inno') -and -not (Test-Path -LiteralPath $Inno -PathType Leaf)) {
    $Inno = Join-Path $env:LOCALAPPDATA 'Programs/MudBuildTools/Inno Setup 6/ISCC.exe'
}
if (-not (Test-Path -LiteralPath $Inno -PathType Leaf)) {
    throw 'Pass the path to verified Inno Setup 6.3 or later with -Inno.'
}
$innoPath = (Resolve-Path -LiteralPath $Inno).Path
$wantsSignature = $SignTool -or $CertificateThumbprint -or $TimestampUrl
if ($wantsSignature -and (-not $SignTool -or -not $CertificateThumbprint -or -not $TimestampUrl)) {
    throw 'Digital signing requires -SignTool, -CertificateThumbprint and -TimestampUrl.'
}
if ($wantsSignature) { $SignTool = (Resolve-Path -LiteralPath $SignTool).Path }

$out = (New-Item -ItemType Directory -Path $OutputDirectory -Force).FullName
$rootPrefix = $gameRoot.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
if ($out.Equals($gameRoot, [StringComparison]::OrdinalIgnoreCase) -or
    $out.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Write the installer outside the packaged game folder.'
}
$installerName = "MudConstructionSimulator-$Version-Setup.exe"
$installer = Join-Path $out $installerName
$summary = Join-Path $out "MudConstructionSimulator-$Version-release.json"
if (Test-Path -LiteralPath $installer) { throw "Installer already exists: $installer" }
if (Test-Path -LiteralPath $summary) { throw "Release metadata already exists: $summary" }
if ($wantsSignature) {
    foreach ($exe in $boot, $shipping) {
        & $SignTool sign /sha1 $CertificateThumbprint /fd SHA256 /tr $TimestampUrl /td SHA256 $exe
        if ($LASTEXITCODE -ne 0) { throw "Digital signing failed: $exe" }
        & $SignTool verify /pa /tw $exe
        if ($LASTEXITCODE -ne 0) { throw "Signature verification failed: $exe" }
    }
}
$report = Join-Path $out "smoke-$([Guid]::NewGuid().ToString('N')).txt"
$processArgs = @{
    FilePath = $shipping
    WorkingDirectory = $gameRoot
    PassThru = $true
    WindowStyle = 'Hidden'
    ArgumentList = @('-NullRHI','-nosound','-unattended','-MudSmokeTest',('-MudSmokeReport="{0}"' -f $report))
}
$process = Start-Process @processArgs
try {
    if (-not $process.WaitForExit(180000)) {
        $process.Kill()
        throw 'The packaged game smoke test timed out.'
    }
    if ($process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $report -PathType Leaf) -or
        (Get-Content -LiteralPath $report -Raw).Trim() -ne 'passed') {
        throw "The packaged game smoke test failed (exit $($process.ExitCode))."
    }
}
finally { $process.Dispose() }

$script = Join-Path $PSScriptRoot 'windows-installer.iss'
$compilerArgs = @("/DGameFiles=$gameRoot", "/DBuildVersion=$Version", "/O$out")
if ($wantsSignature) {
    $compilerArgs += '/DSignedBuild=1'
    $compilerArgs += ('/SMudSign=$q{0}$q sign /sha1 {1} /fd SHA256 /tr $q{2}$q /td SHA256 $f' -f
        $SignTool, $CertificateThumbprint, $TimestampUrl)
}
& $innoPath @compilerArgs $script
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $installer -PathType Leaf)) {
    throw 'Inno Setup did not produce an installer.'
}

$publisherSha256 = $null
if ($wantsSignature) {
    & $SignTool verify /pa /tw $installer
    if ($LASTEXITCODE -ne 0) { throw 'Installer signature verification failed.' }
    $publishedSignature = Get-AuthenticodeSignature -LiteralPath $installer
    if ($publishedSignature.Status -ne 'Valid' -or $publishedSignature.SignerCertificate.Thumbprint -ne $CertificateThumbprint) {
        throw 'Installer certificate does not match the release certificate.'
    }
    $hasher = [System.Security.Cryptography.SHA256]::Create()
    try {
        $publisherSha256 = [BitConverter]::ToString($hasher.ComputeHash($publishedSignature.SignerCertificate.RawData)).Replace('-', '')
    }
    finally { $hasher.Dispose() }
}
$sha256 = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash
[ordered]@{
    product = 'MudConstructionSimulator'
    version = $Version.ToString()
    installer = $installerName
    sha256 = $sha256
    signed = [bool]$wantsSignature
    publisherCertificateSha256 = $publisherSha256
} | ConvertTo-Json | Set-Content -LiteralPath $summary -Encoding UTF8
Write-Output "Installer: $installer"
Write-Output "Release metadata: $summary"
if (-not $wantsSignature) { Write-Output 'Unsigned build for local testing. No publisher identity is attached.' }
