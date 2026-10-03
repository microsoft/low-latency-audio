<#
.SYNOPSIS
    Authenticode-signs build output with a code signing certificate chosen by its thumbprint.

.DESCRIPTION
    Wraps signtool.exe so that build-all.ps1 and the installer projects all sign the same way.

    The certificate has to be in a Windows certificate store, with its private key available to
    signtool. That includes an EV certificate on a hardware token: the token's software puts the
    certificate in the current user's store, and signtool asks for the token password when it
    signs.

    Signing is never implicit. Every caller passes -CertificateThumbprint, or sets
    LLA_SIGNING_THUMBPRINT, and nothing here runs unless one of those is present.

    Files that already carry a valid signature are skipped, so redistributables that Microsoft
    already signed, such as the Windows App SDK bootstrapper and WebView2, keep their signature
    instead of being signed again with ours. Pass -Force to sign them anyway.

    Every signature is timestamped, so it stays valid after the certificate expires.

.PARAMETER Path
    Files and/or folders. Folders are walked recursively and filtered to signable file types.

.PARAMETER CertificateThumbprint
    The SHA-1 thumbprint of the code signing certificate. Spaces are ignored, so it can be pasted
    from the certificate's Details tab. Defaults to LLA_SIGNING_THUMBPRINT.

.PARAMETER MachineStore
    Look for the certificate in the local machine store instead of the current user's store. Also
    turned on by setting LLA_SIGNING_MACHINE_STORE to 1.

.PARAMETER TimestampUrl
    An RFC 3161 timestamp server. Defaults to LLA_TIMESTAMP_URL, then to DigiCert's public server.
    Most certificate vendors document their own; any of them works with any certificate.

.PARAMETER Description
    Shown as the program name in the Windows administrator prompt for a signed .msi. Defaults to
    LLA_SIGNING_DESCRIPTION, then to the product name.

.PARAMETER SignToolPath
    signtool.exe from the Windows SDK. Defaults to LLA_SIGNTOOL, then to the newest Windows Kits
    install.

.EXAMPLE
    .\sign-files.ps1 -Path ..\build\staging\installer\x64\app -CertificateThumbprint 0123456789ABCDEF0123456789ABCDEF01234567

.EXAMPLE
    .\build-all.ps1 -Sign -CertificateThumbprint 0123456789ABCDEF0123456789ABCDEF01234567
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory, Position = 0)]
    [string[]] $Path,

    [string] $CertificateThumbprint = $env:LLA_SIGNING_THUMBPRINT,

    [switch] $MachineStore,

    [string] $TimestampUrl = $env:LLA_TIMESTAMP_URL,

    [string] $Description = $env:LLA_SIGNING_DESCRIPTION,

    [string] $DescriptionUrl = 'https://aka.ms/asio',

    [string] $SignToolPath = $env:LLA_SIGNTOOL,

    # Sign files that already have a valid signature. Off by default so redistributables keep the
    # signature they shipped with.
    [switch] $Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$DefaultTimestampUrl = 'http://timestamp.digicert.com'
$DefaultDescription = 'Microsoft Low-Latency Audio'

# PE files and installer formats. Types that can't carry an Authenticode signature (.pri, .pdb,
# .png and so on) are skipped rather than failing, because callers hand over whole folders.
$SignableExtensions = @('.exe', '.dll', '.sys', '.msi', '.msm', '.msp', '.cab', '.cat', '.ps1', '.psm1', '.psd1')

# signtool signs many files per run. With a hardware token, fewer runs also means fewer
# password prompts.
$BatchSize = 40

function Get-HostArchitecture {
    if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64') { return 'arm64' }
    return 'x64'
}

function Resolve-SignTool {
    if ($SignToolPath) {
        if (-not (Test-Path $SignToolPath)) { throw "signtool.exe not found at -SignToolPath: $SignToolPath" }
        return (Resolve-Path $SignToolPath).Path
    }

    $arch = Get-HostArchitecture

    $roots = @(
        (Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'),
        (Join-Path $env:ProgramFiles 'Windows Kits\10\bin')
    ) | Where-Object { $_ -and (Test-Path $_) }

    $found = foreach ($root in $roots) {
        Get-ChildItem $root -Directory -Filter '10.*' -ErrorAction SilentlyContinue |
            ForEach-Object {
                $exe = Join-Path $_.FullName "$arch\signtool.exe"
                if (Test-Path $exe) {
                    [pscustomobject]@{ Version = [version]$_.Name; Path = $exe }
                }
            }
    }

    $best = @($found) | Sort-Object Version -Descending | Select-Object -First 1

    if (-not $best) {
        throw "Could not find signtool.exe for $arch under Windows Kits\10\bin. Install the Windows SDK, or pass -SignToolPath / set LLA_SIGNTOOL."
    }

    return $best.Path
}

# Thumbprints copied from the certificate dialog carry spaces and, often, an invisible
# left-to-right mark at the start. Keep only the hex digits.
function Get-NormalizedThumbprint {
    param([Parameter(Mandatory)] [string] $Value)

    $normalized = ($Value -replace '[^0-9A-Fa-f]', '').ToUpperInvariant()

    if ($normalized.Length -ne 40) {
        throw "'$Value' is not a certificate thumbprint. It must be 40 hexadecimal digits."
    }

    return $normalized
}

function Assert-CertificateUsable {
    param(
        [Parameter(Mandatory)] [string] $Thumbprint,
        [Parameter(Mandatory)] [string] $StoreLocation
    )

    $certificate = Get-Item "Cert:\$StoreLocation\My\$Thumbprint" -ErrorAction SilentlyContinue

    if (-not $certificate) {
        throw @"
No certificate with thumbprint $Thumbprint in the $StoreLocation\My certificate store.

If the certificate is on a hardware token, plug the token in and make sure its software has put
the certificate in the store (certmgr.msc, Personal, Certificates). If it is in the local machine
store instead, pass -MachineStore or set LLA_SIGNING_MACHINE_STORE=1.
"@
    }

    if (-not $certificate.HasPrivateKey) {
        throw "The certificate $Thumbprint ($($certificate.Subject)) has no private key on this PC, so it can't sign."
    }

    $codeSigning = '1.3.6.1.5.5.7.3.3'
    $usages = @($certificate.EnhancedKeyUsageList | ForEach-Object { $_.ObjectId })

    if ($usages.Count -gt 0 -and $usages -notcontains $codeSigning) {
        throw "The certificate $Thumbprint ($($certificate.Subject)) is not a code signing certificate."
    }

    if ($certificate.NotAfter -lt (Get-Date)) {
        throw "The certificate $Thumbprint ($($certificate.Subject)) expired on $($certificate.NotAfter)."
    }

    return $certificate
}

function Get-FileToSign {
    param([Parameter(Mandatory)] [string[]] $InputPath)

    $files = foreach ($item in $InputPath) {
        if (-not (Test-Path $item)) { throw "Path to sign does not exist: $item" }

        if (Test-Path $item -PathType Container) {
            Get-ChildItem $item -File -Recurse
        }
        else {
            Get-Item $item
        }
    }

    @($files) |
        Where-Object { $SignableExtensions -contains $_.Extension.ToLowerInvariant() } |
        Sort-Object FullName -Unique
}

# ----------------------------------------------------------------------------------------------

if (-not $CertificateThumbprint) {
    throw 'No signing certificate. Pass -CertificateThumbprint or set LLA_SIGNING_THUMBPRINT to the thumbprint of a code signing certificate.'
}

if (-not $MachineStore -and $env:LLA_SIGNING_MACHINE_STORE -eq '1') {
    $MachineStore = [switch]::new($true)
}

if (-not $TimestampUrl) { $TimestampUrl = $DefaultTimestampUrl }
if (-not $Description) { $Description = $DefaultDescription }

$thumbprint = Get-NormalizedThumbprint -Value $CertificateThumbprint
$storeLocation = if ($MachineStore) { 'LocalMachine' } else { 'CurrentUser' }

$certificate = Assert-CertificateUsable -Thumbprint $thumbprint -StoreLocation $storeLocation

$signTool = Resolve-SignTool

$candidates = @(Get-FileToSign -InputPath $Path)

$skipped = @()
if (-not $Force) {
    $unsigned = foreach ($file in $candidates) {
        if ((Get-AuthenticodeSignature -FilePath $file.FullName).Status -eq 'Valid') {
            $skipped += $file
        }
        else {
            $file
        }
    }
    $candidates = @($unsigned)
}

if ($candidates.Count -eq 0) {
    Write-Host "     Signing: nothing to do ($($skipped.Count) already signed)" -ForegroundColor DarkGray
    return
}

Write-Host "     Signing $($candidates.Count) files with $($certificate.Subject) ($($skipped.Count) already signed, left alone)" -ForegroundColor DarkGray

$commonArgs = @(
    'sign'
    '/v'
    '/sha1', $thumbprint
    '/fd', 'SHA256'
    '/tr', $TimestampUrl
    '/td', 'SHA256'
    '/d', $Description
    '/du', $DescriptionUrl
)

if ($MachineStore) {
    $commonArgs += '/sm'
}

for ($i = 0; $i -lt $candidates.Count; $i += $BatchSize) {
    $batch = @($candidates[$i..([Math]::Min($i + $BatchSize, $candidates.Count) - 1)])

    # Built as one array and splatted. Handing a native command a nested array argument is where
    # paths with spaces stop being separate arguments.
    $arguments = $commonArgs + @($batch | ForEach-Object { $_.FullName })

    & $signTool @arguments

    if ($LASTEXITCODE -ne 0) {
        throw @"
signtool failed ($LASTEXITCODE) signing $($batch.Count) files starting with $($batch[0].FullName).

Common causes:
  * The hardware token is not plugged in, or its password prompt was canceled.
  * The timestamp server $TimestampUrl could not be reached. Pass -TimestampUrl or set
    LLA_TIMESTAMP_URL to your certificate vendor's server.
"@
    }
}

Write-Host "     Signed $($candidates.Count) files" -ForegroundColor DarkGray
