[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Installer,
    [Parameter(Mandatory)][ValidatePattern('^[0-9a-fA-F]{64}$')][string]$Sha256,
    [Parameter(Mandatory)][ValidatePattern('^[0-9a-fA-F]{64}$')][string]$PublisherCertificateSha256
)

$ErrorActionPreference = 'Stop'
$file = Get-Item -LiteralPath $Installer
if ($file.PSIsContainer -or $file.Extension -ne '.exe') { throw 'Choose the release installer .exe.' }

# Hold a read lock through process creation so the verified installer cannot be swapped.
$stream = [IO.File]::Open($file.FullName, 'Open', 'Read', 'Read')
try {
    if ((Get-FileHash -InputStream $stream -Algorithm SHA256).Hash -ne $Sha256) {
        throw 'Installer SHA-256 does not match the trusted release.'
    }
    $signature = Get-AuthenticodeSignature -LiteralPath $file.FullName
    if ($signature.Status -ne 'Valid') { throw "Invalid installer signature: $($signature.Status)" }
    $hasher = [System.Security.Cryptography.SHA256]::Create()
    try { $signer = [BitConverter]::ToString($hasher.ComputeHash($signature.SignerCertificate.RawData)).Replace('-', '') }
    finally { $hasher.Dispose() }
    if ($signer -ne $PublisherCertificateSha256) { throw 'Installer was signed by a different publisher certificate.' }
    $setup = Start-Process -FilePath $file.FullName -WorkingDirectory $file.DirectoryName -PassThru
}
finally { $stream.Dispose() }
try {
    $setup.WaitForExit()
    if ($setup.ExitCode -ne 0) { throw "Setup did not finish (exit $($setup.ExitCode))." }
}
finally { $setup.Dispose() }
