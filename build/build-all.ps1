<#
.SYNOPSIS
    Builds, stages, signs and packages the Low-Latency Audio preview: the USB Audio 2.0 driver, the
    ASIO driver and its control panel, the Low-Latency Audio Driver Selector, and the installer
    that carries all of them.

.DESCRIPTION
    This is the release build. build\build.ps1 is the older developer script and is left as it is.

    The installer is a bundle per platform, x64 and Arm64. Each bundle installs the Visual C++
    runtime and the Windows App Runtime, then one .msi with:

      - the ASIO driver (USBAsio.dll) and the registry entries music apps find it by
      - the ASIO control panel, in the same folder, because the ASIO driver starts it from there
      - the Low-Latency Audio Driver Selector, where the customer chooses which devices use the
        low-latency driver
      - the low-latency driver package, which the installer adds to the driver store without
        putting it on any device

    The driver is a kernel driver, so customers' PCs only load it once Microsoft has signed it
    through attestation signing in Partner Center. That is a manual step in the middle of a
    release:

      1. .\build-all.ps1 -Target Driver,DriverCab -Sign
         Builds the driver and puts each platform's driver into a .cab, signed with the EV
         certificate attestation needs, in build\release\<version>\attestation.
      2. Submit each .cab in Partner Center, then download the signed results and unzip them all
         into one folder. Each one unzips into a folder named for its platform.
      3. .\build-all.ps1 -Target Asio,Apps,Stage,Setup,Release -Sign -AttestedDriverPath <folder>
         Builds everything else and the installer around the Microsoft-signed driver.

    Without -AttestedDriverPath, Stage takes the driver straight from the local build. That
    driver is test-signed, so the installer only works on PCs with test signing turned on. With
    -Sign, Stage refuses to do that unless -AllowTestSignedDriver is passed as well.

.PARAMETER Target
    One or more of the following. They always run in this order, whatever order they're given in.

      Clean      Delete build\staging\installer and the output of every project this script builds.
      Driver     Build src\uac2-driver\USBAudioAcxDriver.sln. Needs the Windows Driver Kit.
      DriverCab  Put the built driver into a .cab per platform for attestation signing.
      Asio       Build src\uac2-asio\USBAsio.sln for x64, and for Arm64EC, which produces the
                 Arm64X DLL the Arm64 installer carries. Needs the Steinberg ASIO SDK in
                 src\uac2-asio\asio.
      Apps       Build the ASIO control panel and the Driver Selector.
      Stage      Copy everything the installer carries into build\staging\installer\<platform>
                 and check it. With -Sign, sign the binaries there.
      Setup      Download the runtime installers if they're missing, then build the custom
                 actions, the .msi and the bundle.
      Release    Copy the installers and the symbols into build\release\<version>.
      All        Driver, Asio, Apps, Stage, Setup, Release.

.PARAMETER Platform
    x64, Arm64, or both. Defaults to both.

.PARAMETER AttestedDriverPath
    The folder holding the driver Microsoft signed through attestation, with a subfolder per
    platform: x64 and Arm64. Stage looks for USBAudio2-ACX.inf anywhere under each.

.PARAMETER AllowTestSignedDriver
    Lets a -Sign build stage the test-signed driver from the local build. For internal testing
    only: customers' PCs don't load a test-signed driver.

.PARAMETER BuildNumber
    Overrides the 'build' field of build\version.json without changing the file. For CI, for
    example -BuildNumber $env:GITHUB_RUN_NUMBER.

.PARAMETER BumpBuildNumber
    Adds one to the 'build' field of build\version.json and saves it before anything else runs.

.PARAMETER Sign
    Authenticode-sign what ships: the attestation .cab files, the staged binaries, the setup
    custom actions, the .msi and the bundle. Uses the certificate -CertificateThumbprint names;
    see build\sign-files.ps1.

.PARAMETER CertificateThumbprint
    The thumbprint of the code signing certificate. Defaults to LLA_SIGNING_THUMBPRINT.
    Attestation signing needs an EV certificate.

.PARAMETER TimestampUrl
    The timestamp server signtool uses. Defaults to LLA_TIMESTAMP_URL, then to DigiCert's.

.PARAMETER RefreshDependencies
    Download the Visual C++ runtime and Windows App Runtime installers again even when a copy is
    already in build\dependencies.

.PARAMETER MaxCpuCount
    How many projects MSBuild builds at once. Defaults to three quarters of the logical
    processors, so the PC stays usable while a build runs.

.EXAMPLE
    .\build-all.ps1 -Target Apps,Stage,Setup -Platform x64
    Build the apps and an x64 installer around whatever driver and ASIO driver were last built.

.EXAMPLE
    .\build-all.ps1 -Target Driver,DriverCab -Sign -CertificateThumbprint 0123456789ABCDEF0123456789ABCDEF01234567
    Build the driver and the signed .cab files to submit for attestation signing.

.EXAMPLE
    .\build-all.ps1 -Target Asio,Apps,Stage,Setup,Release -Sign -AttestedDriverPath C:\signed-driver
    Build a signed release around the driver Microsoft signed.
#>
[CmdletBinding()]
param(
    # Comma-separated works as one value, so this also works through pwsh -File, which does not
    # split array arguments.
    [string[]] $Target = @('All'),

    [string[]] $Platform = @('x64', 'Arm64'),

    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Release',

    [string] $AttestedDriverPath,

    [switch] $AllowTestSignedDriver,

    [int] $BuildNumber = -1,

    [switch] $BumpBuildNumber,

    [switch] $Sign,

    [string] $CertificateThumbprint = $env:LLA_SIGNING_THUMBPRINT,

    [string] $TimestampUrl = $env:LLA_TIMESTAMP_URL,

    [switch] $RefreshDependencies,

    # Explicit MSBuild.exe. Leave empty to let vswhere find the newest Visual Studio.
    [string] $MSBuildPath,

    [ValidateRange(0, 256)]
    [int] $MaxCpuCount = [Math]::Max(1, [int][Math]::Floor([Environment]::ProcessorCount * 0.75)),

    [ValidateSet('Normal', 'BelowNormal', 'Idle')]
    [string] $Priority = 'BelowNormal',

    [ValidateSet('quiet', 'minimal', 'normal', 'detailed', 'diagnostic')]
    [string] $Verbosity = 'minimal'
)

#Requires -Version 7.2

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$InformationPreference = 'Continue'

$AllTargets = @('Clean', 'Driver', 'DriverCab', 'Asio', 'Apps', 'Stage', 'Setup', 'Release')
$DefaultTargets = @('Driver', 'Asio', 'Apps', 'Stage', 'Setup', 'Release')

# ----------------------------------------------------------------------------------------------
# Paths
# ----------------------------------------------------------------------------------------------

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$SourceRoot = Join-Path $RepoRoot 'src'
$OutRoot = Join-Path $SourceRoot 'vsfiles\out'
$IntermediateRoot = Join-Path $SourceRoot 'vsfiles\intermediate'
$StagingRoot = Join-Path $RepoRoot 'build\staging\installer'
$DependenciesRoot = Join-Path $RepoRoot 'build\dependencies'
$ReleaseRoot = Join-Path $RepoRoot 'build\release'
$VersionFile = Join-Path $PSScriptRoot 'version.json'
$SignScript = Join-Path $PSScriptRoot 'sign-files.ps1'

$DriverSolution = Join-Path $SourceRoot 'uac2-driver\USBAudioAcxDriver.sln'
$AsioSolution = Join-Path $SourceRoot 'uac2-asio\USBAsio.sln'
$ControlPanelSolution = Join-Path $SourceRoot 'asio-control-panel\USBAsioControlPanel.sln'
$SelectorSolution = Join-Path $SourceRoot 'driver-selector\LowLatencyDriverSelector.sln'
$InstallerFolder = Join-Path $SourceRoot 'installers\low-latency-audio-installer'
$CustomActionsProject = Join-Path $InstallerFolder 'custom-actions\LowLatencyAudioSetupActions.vcxproj'
$BundleProject = Join-Path $InstallerFolder 'bundle\LowLatencyAudioBundle.wixproj'
$BrandingFile = Join-Path $InstallerFolder 'Branding.wxi'

# The two WinUI apps install into one folder, so they must be built against the same Windows App
# SDK. Both of these files name the version, and the runtime installer the bundle carries follows it.
$AppPackagesConfigs = @(
    (Join-Path $SourceRoot 'asio-control-panel\packages.config'),
    (Join-Path $SourceRoot 'driver-selector\packages.config')
)

# Set by the driver project.
$DriverInfName = 'USBAudio2-ACX.inf'
$DriverSysName = 'USBAudio2-ACX.sys'
$DriverCatName = 'USBAudio2-ACX.cat'
$DriverProjectName = 'USBAudioAcxDriver'

# Files the two apps both carry. They have to be byte for byte the same.
$SharedAppFiles = @('Microsoft.WindowsAppRuntime.Bootstrap.dll', 'Microsoft.Web.WebView2.Core.dll')

# The signer of every driver Microsoft signs through attestation.
$AttestationSigner = 'Microsoft Windows Hardware Compatibility Publisher'

# IMAGE_FILE_MACHINE values. An Arm64X DLL reports Arm64.
$MachineTypes = @{ 'x64' = 0x8664; 'ARM64' = 0xAA64 }

$RunStarted = Get-Date

# ----------------------------------------------------------------------------------------------
# Output
# ----------------------------------------------------------------------------------------------

function Write-Step {
    param([Parameter(Mandatory)] [string] $Message)
    Write-Host ''
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Write-Detail {
    param([Parameter(Mandatory)] [string] $Message)
    Write-Host "     $Message" -ForegroundColor DarkGray
}

# ----------------------------------------------------------------------------------------------
# Arguments
# ----------------------------------------------------------------------------------------------

function Expand-Argument {
    param([string[]] $Value, [string[]] $Allowed, [string] $Name)

    $expanded = @($Value | ForEach-Object { $_ -split ',' } | Where-Object { $_ } | ForEach-Object { $_.Trim() })

    foreach ($item in $expanded) {
        if ($Allowed -notcontains $item) {
            throw "Unknown $Name '$item'. Use one or more of: $($Allowed -join ', ')"
        }
    }

    return $expanded
}

function Get-NormalizedPlatform {
    param([Parameter(Mandatory)] [string] $Value)

    switch ($Value.ToLowerInvariant()) {
        'x64' { return 'x64' }
        'arm64' { return 'ARM64' }
        default { throw "Unknown platform '$Value'. Use x64, Arm64, or both." }
    }
}

# ----------------------------------------------------------------------------------------------
# Version and branding
# ----------------------------------------------------------------------------------------------

function Get-ReleaseVersion {
    $json = Get-Content $VersionFile -Raw | ConvertFrom-Json

    $build = if ($BuildNumber -ge 0) { $BuildNumber } else { [int]$json.build }

    $text = '{0}.{1}.{2}' -f $json.major, $json.minor, $json.patch

    if ($json.channel -ne 'stable') {
        $text += '-{0}.{1}' -f $json.channel, $json.channelNumber
    }

    [pscustomobject]@{
        Major   = [int]$json.major
        Minor   = [int]$json.minor
        Patch   = [int]$json.patch
        Build   = $build
        Text    = $text
        Numeric = '{0}.{1}.{2}.{3}' -f $json.major, $json.minor, $json.patch, $build
    }
}

function Step-BuildNumber {
    $content = Get-Content $VersionFile -Raw

    $match = [regex]::Match($content, '"build"\s*:\s*(\d+)')
    if (-not $match.Success) {
        throw "Could not find the 'build' field in $VersionFile"
    }

    $next = [int]$match.Groups[1].Value + 1
    $updated = $content.Substring(0, $match.Groups[1].Index) + $next + $content.Substring($match.Groups[1].Index + $match.Groups[1].Length)

    Set-Content -Path $VersionFile -Value $updated -NoNewline -Encoding utf8NoBOM
    Write-Detail "Build number is now $next"
}

# MSBuild properties that carry the version into the projects that take it.
function Get-VersionProperties {
    param([Parameter(Mandatory)] $Version)

    @{
        LlaVersionMajor    = $Version.Major
        LlaVersionMinor    = $Version.Minor
        LlaVersionBuild    = $Version.Patch
        LlaVersionRevision = $Version.Build
    }
}

function Get-BrandingValue {
    param([Parameter(Mandatory)] [string] $Name)

    $content = Get-Content $BrandingFile -Raw
    $match = [regex]::Match($content, "<\?define\s+$Name\s*=\s*`"([^`"]*)`"\s*\?>")

    if (-not $match.Success) {
        throw "Could not find $Name in $BrandingFile"
    }

    return $match.Groups[1].Value
}

# ----------------------------------------------------------------------------------------------
# Tools
# ----------------------------------------------------------------------------------------------

function Resolve-MSBuild {
    if ($MSBuildPath) {
        if (-not (Test-Path $MSBuildPath)) { throw "MSBuild.exe not found at -MSBuildPath: $MSBuildPath" }
        return (Resolve-Path $MSBuildPath).Path
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) {
        throw 'vswhere.exe was not found. Install Visual Studio, or pass -MSBuildPath.'
    }

    $hostFolder = if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64') { 'arm64' } else { 'amd64' }

    $found = & $vswhere -latest -prerelease -products * -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\$hostFolder\MSBuild.exe" |
        Select-Object -First 1

    if (-not $found) {
        throw 'vswhere did not find MSBuild. Install the Visual Studio C++ desktop workload, or pass -MSBuildPath.'
    }

    return $found
}

function Resolve-WindowsKitTool {
    param([Parameter(Mandatory)] [string] $Name)

    $arch = if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64') { 'arm64' } else { 'x64' }

    $roots = @(
        (Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'),
        (Join-Path $env:ProgramFiles 'Windows Kits\10\bin')
    ) | Where-Object { $_ -and (Test-Path $_) }

    $found = foreach ($root in $roots) {
        Get-ChildItem $root -Directory -Filter '10.*' -ErrorAction SilentlyContinue |
            ForEach-Object {
                $exe = Join-Path $_.FullName "$arch\$Name"
                if (Test-Path $exe) {
                    [pscustomobject]@{ Version = [version]$_.Name; Path = $exe }
                }
            }
    }

    $best = @($found) | Sort-Object Version -Descending | Select-Object -First 1

    if (-not $best) {
        throw "Could not find $Name under Windows Kits\10\bin. Install the Windows SDK."
    }

    return $best.Path
}

function Invoke-MSBuild {
    param(
        [Parameter(Mandatory)] [string] $Project,
        [Parameter(Mandatory)] [string] $BuildPlatform,
        [string[]] $Targets = @('Build'),
        [hashtable] $Properties = @{},
        [switch] $Restore
    )

    $arguments = @(
        $Project
        "/t:$($Targets -join ';')"
        "/p:Configuration=$Configuration"
        "/p:Platform=$BuildPlatform"
        $(if ($MaxCpuCount -eq 0) { '/m' } else { "/m:$MaxCpuCount" })
        '/nologo'
        '/nr:false'
        "/v:$Verbosity"
    )

    if ($Restore) {
        $arguments += '/restore'
    }

    foreach ($key in ($Properties.Keys | Sort-Object)) {
        $arguments += "/p:$key=$($Properties[$key])"
    }

    Write-Detail "msbuild $(Split-Path $Project -Leaf) /t:$($Targets -join ';') ($Configuration|$BuildPlatform)"

    & $script:MSBuild @arguments

    if ($LASTEXITCODE -ne 0) {
        throw "MSBuild failed ($LASTEXITCODE): $Project ($Configuration|$BuildPlatform)"
    }
}

# packages.config projects restore in a separate pass, before the build evaluates them.
function Invoke-PackagesConfigRestore {
    param(
        [Parameter(Mandatory)] [string] $Solution,
        [Parameter(Mandatory)] [string] $BuildPlatform
    )

    Invoke-MSBuild -Project $Solution -BuildPlatform $BuildPlatform -Targets @('Restore') -Properties @{ RestorePackagesConfig = 'true' }
}

function Invoke-Sign {
    param([Parameter(Mandatory)] [string[]] $Path)

    if (-not $Sign) {
        return
    }

    & $SignScript -Path $Path
}

# ----------------------------------------------------------------------------------------------
# Checks
# ----------------------------------------------------------------------------------------------

function Get-PeMachine {
    param([Parameter(Mandatory)] [string] $Path)

    $stream = [System.IO.File]::OpenRead($Path)

    try {
        $reader = [System.IO.BinaryReader]::new($stream)

        if ($reader.ReadUInt16() -ne 0x5A4D) {
            throw "$Path is not a Windows executable."
        }

        $stream.Position = 0x3C
        $peOffset = $reader.ReadInt32()

        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550) {
            throw "$Path is not a Windows executable."
        }

        return [int]$reader.ReadUInt16()
    }
    finally {
        $stream.Dispose()
    }
}

function Assert-Machine {
    param(
        [Parameter(Mandatory)] [string] $Path,
        [Parameter(Mandatory)] [string] $BuildPlatform
    )

    $expected = $MachineTypes[$BuildPlatform]
    $actual = Get-PeMachine -Path $Path

    if ($actual -ne $expected) {
        throw ('{0} is built for machine 0x{1:X4}, not {2} (0x{3:X4}).' -f $Path, $actual, $BuildPlatform, $expected)
    }
}

function Copy-Required {
    param(
        [Parameter(Mandatory)] [string] $Source,
        [Parameter(Mandatory)] [string] $DestinationFolder
    )

    if (-not (Test-Path $Source -PathType Leaf)) {
        throw "Missing build output: $Source`nBuild it first, or add its target to -Target."
    }

    Copy-Item $Source -Destination $DestinationFolder -Force
    Write-Detail ('{0,-45} {1:yyyy-MM-dd HH:mm}' -f (Split-Path $Source -Leaf), (Get-Item $Source).LastWriteTime)
}

function Copy-Symbols {
    param(
        [Parameter(Mandatory)] [string] $SourceFolder,
        [Parameter(Mandatory)] [string] $Name,
        [Parameter(Mandatory)] [string] $DestinationFolder
    )

    $pdb = Join-Path $SourceFolder "$Name.pdb"

    if (Test-Path $pdb) {
        Copy-Item $pdb -Destination $DestinationFolder -Force
    }
}

# Each app's XAML has to be compiled into its own <exe name>.pri. A loose App.xbf or
# MainWindow.xbf would be shared by both apps in the install folder, and one would load the
# other's window.
function Assert-XamlInPri {
    param([Parameter(Mandatory)] [string] $PriPath)

    $dump = Join-Path ([System.IO.Path]::GetTempPath()) ("lla-pri-{0}.xml" -f [guid]::NewGuid())

    try {
        & $script:MakePri dump /if $PriPath /of $dump /dt detailed /o | Out-Null

        if ($LASTEXITCODE -ne 0 -or -not (Test-Path $dump)) {
            throw "makepri could not read $PriPath"
        }

        [xml]$xml = Get-Content $dump -Raw

        $xbf = @($xml.SelectNodes("//NamedResource[substring(@name, string-length(@name) - 3) = '.xbf']"))

        if ($xbf.Count -eq 0) {
            throw "$PriPath holds no compiled XAML."
        }

        foreach ($resource in $xbf) {
            foreach ($candidate in @($resource.SelectNodes('Candidate'))) {
                if ($candidate.type -ne 'EmbeddedData') {
                    throw "$PriPath points at $($resource.name) on disk instead of holding it. Both apps install into one folder, so their XAML must be inside their own .pri files."
                }
            }
        }
    }
    finally {
        Remove-Item $dump -Force -ErrorAction SilentlyContinue
    }
}

function Test-AttestationSigned {
    param([Parameter(Mandatory)] [string] $CatalogPath)

    $signature = Get-AuthenticodeSignature -FilePath $CatalogPath

    return ($signature.Status -eq 'Valid') -and
        ($null -ne $signature.SignerCertificate) -and
        ($signature.SignerCertificate.Subject -like "*$AttestationSigner*")
}

# ----------------------------------------------------------------------------------------------
# Targets
# ----------------------------------------------------------------------------------------------

function Invoke-CleanTarget {
    Write-Step 'Clean'

    $projects = @(
        $DriverProjectName,
        'USBAsio',
        'USBAsioControlPanel',
        'LowLatencyDriverSelector',
        'LowLatencyAudioSetupActions',
        'LowLatencyAudioPackage',
        'LowLatencyAudioBundle'
    )

    $folders = @($StagingRoot) +
        @($projects | ForEach-Object { Join-Path $OutRoot $_ }) +
        @($projects | ForEach-Object { Join-Path $IntermediateRoot $_ })

    foreach ($folder in $folders) {
        if (Test-Path $folder) {
            Remove-Item $folder -Recurse -Force
            Write-Detail "Deleted $folder"
        }
    }
}

function Invoke-DriverTarget {
    Write-Step 'Driver'

    foreach ($buildPlatform in $Platforms) {
        Invoke-PackagesConfigRestore -Solution $DriverSolution -BuildPlatform $buildPlatform
        Invoke-MSBuild -Project $DriverSolution -BuildPlatform $buildPlatform
    }
}

function Get-DriverBuildFolder {
    param([Parameter(Mandatory)] [string] $BuildPlatform)

    Join-Path $OutRoot "$DriverProjectName\$BuildPlatform\$Configuration"
}

function Invoke-DriverCabTarget {
    param([Parameter(Mandatory)] $Version)

    Write-Step 'DriverCab'

    $makeCab = Join-Path $env:SystemRoot 'System32\makecab.exe'
    $cabFolder = Join-Path $ReleaseRoot "$($Version.Text)\attestation"
    New-Item $cabFolder -ItemType Directory -Force | Out-Null

    foreach ($buildPlatform in $Platforms) {
        $buildFolder = Get-DriverBuildFolder -BuildPlatform $buildPlatform
        $packageFolder = Join-Path $buildFolder $DriverProjectName

        # Microsoft makes the catalog during attestation signing, so it stays out of the .cab.
        $files = @(
            (Join-Path $packageFolder $DriverInfName),
            (Join-Path $packageFolder $DriverSysName)
        )

        foreach ($file in $files) {
            if (-not (Test-Path $file)) {
                throw "Missing driver build output: $file`nRun the Driver target first."
            }
        }

        Assert-Machine -Path (Join-Path $packageFolder $DriverSysName) -BuildPlatform $buildPlatform

        $pdb = Join-Path $buildFolder ([System.IO.Path]::ChangeExtension($DriverSysName, '.pdb'))
        if (Test-Path $pdb) {
            $files += $pdb
        }

        $cabName = "USBAudio2-ACX-$($Version.Text)-$buildPlatform.cab"
        $ddf = Join-Path ([System.IO.Path]::GetTempPath()) "lla-$buildPlatform.ddf"

        # The files go in a folder named for the platform, so the signed downloads unzip side by
        # side into the layout -AttestedDriverPath expects.
        $lines = @(
            '.OPTION EXPLICIT'
            '.Set CabinetFileCountThreshold=0'
            '.Set FolderFileCountThreshold=0'
            '.Set FolderSizeThreshold=0'
            '.Set MaxCabinetSize=0'
            '.Set MaxDiskFileCount=0'
            '.Set MaxDiskSize=0'
            '.Set CompressionType=MSZIP'
            '.Set Cabinet=on'
            '.Set Compress=on'
            ".Set CabinetNameTemplate=$cabName"
            ".Set DiskDirectoryTemplate=`"$cabFolder`""
            '.Set InfFileName=NUL'
            '.Set RptFileName=NUL'
            ".Set DestinationDir=$buildPlatform"
        ) + @($files | ForEach-Object { "`"$_`"" })

        Set-Content -Path $ddf -Value $lines -Encoding ascii

        try {
            & $makeCab /F $ddf | Out-Null

            if ($LASTEXITCODE -ne 0) {
                throw "makecab failed ($LASTEXITCODE) for $buildPlatform"
            }
        }
        finally {
            Remove-Item $ddf -Force -ErrorAction SilentlyContinue
        }

        $cab = Join-Path $cabFolder $cabName
        Invoke-Sign -Path $cab

        Write-Detail "Wrote $cab"
    }

    if (-not $Sign) {
        Write-Warning 'The .cab files are not signed. Attestation signing only accepts a .cab signed with the EV certificate registered in Partner Center. Run again with -Sign.'
    }
}

function Invoke-AsioTarget {
    Write-Step 'Asio'

    foreach ($buildPlatform in $Platforms) {
        # The Arm64 installer carries the Arm64X build, which comes out of the Arm64EC build.
        $asioPlatform = if ($buildPlatform -eq 'ARM64') { 'ARM64EC' } else { 'x64' }

        Invoke-PackagesConfigRestore -Solution $AsioSolution -BuildPlatform $asioPlatform
        Invoke-MSBuild -Project $AsioSolution -BuildPlatform $asioPlatform
    }
}

function Invoke-AppsTarget {
    param([Parameter(Mandatory)] $Version)

    Write-Step 'Apps'

    $versionProperties = Get-VersionProperties -Version $Version

    foreach ($buildPlatform in $Platforms) {
        Invoke-PackagesConfigRestore -Solution $ControlPanelSolution -BuildPlatform $buildPlatform
        Invoke-MSBuild -Project $ControlPanelSolution -BuildPlatform $buildPlatform

        Invoke-PackagesConfigRestore -Solution $SelectorSolution -BuildPlatform $buildPlatform
        Invoke-MSBuild -Project $SelectorSolution -BuildPlatform $buildPlatform -Properties $versionProperties
    }
}

function Get-DriverSourceFolder {
    param([Parameter(Mandatory)] [string] $BuildPlatform)

    if (-not $AttestedDriverPath) {
        return Join-Path (Get-DriverBuildFolder -BuildPlatform $BuildPlatform) $DriverProjectName
    }

    $platformFolder = Join-Path $AttestedDriverPath $BuildPlatform

    if (-not (Test-Path $platformFolder)) {
        throw "No $BuildPlatform folder in -AttestedDriverPath $AttestedDriverPath. Unzip the signed $BuildPlatform driver there."
    }

    $infs = @(Get-ChildItem $platformFolder -Recurse -File -Filter $DriverInfName)

    if ($infs.Count -ne 1) {
        throw "Expected exactly one $DriverInfName under $platformFolder, found $($infs.Count)."
    }

    return $infs[0].DirectoryName
}

function Invoke-StageTarget {
    Write-Step 'Stage'

    foreach ($buildPlatform in $Platforms) {
        Write-Detail "--- $buildPlatform ---"

        $root = Join-Path $StagingRoot $buildPlatform
        if (Test-Path $root) {
            Remove-Item $root -Recurse -Force
        }

        $app = New-Item (Join-Path $root 'app') -ItemType Directory -Force
        $assets = New-Item (Join-Path $app 'Assets') -ItemType Directory -Force
        $driver = New-Item (Join-Path $root 'driver') -ItemType Directory -Force
        $symbols = New-Item (Join-Path $root 'symbols') -ItemType Directory -Force

        # The ASIO driver. The Arm64 one is the Arm64X DLL from the Arm64EC build.
        $asioPlatform = if ($buildPlatform -eq 'ARM64') { 'ARM64EC' } else { 'x64' }
        $asioOut = Join-Path $OutRoot "USBAsio\$asioPlatform\$Configuration"

        Copy-Required -Source (Join-Path $asioOut 'USBAsio.dll') -DestinationFolder $app
        Copy-Symbols -SourceFolder $asioOut -Name 'USBAsio' -DestinationFolder $symbols

        # The ASIO control panel. Its About page loads an image from Assets on disk.
        $controlPanelOut = Join-Path $OutRoot "USBAsioControlPanel\$buildPlatform\$Configuration"

        foreach ($name in @('USBAsioControlPanel.exe', 'USBAsioControlPanel.pri') + $SharedAppFiles) {
            Copy-Required -Source (Join-Path $controlPanelOut $name) -DestinationFolder $app
        }

        $controlPanelAssets = Join-Path $controlPanelOut 'Assets'
        if (-not (Test-Path $controlPanelAssets)) {
            throw "Missing build output: $controlPanelAssets"
        }
        Copy-Item (Join-Path $controlPanelAssets '*') -Destination $assets -Recurse -Force
        Copy-Symbols -SourceFolder $controlPanelOut -Name 'USBAsioControlPanel' -DestinationFolder $symbols

        # The Driver Selector.
        $selectorOut = Join-Path $OutRoot "LowLatencyDriverSelector\$buildPlatform\$Configuration"

        foreach ($name in @('LowLatencyDriverSelector.exe', 'LowLatencyDriverSelector.pri')) {
            Copy-Required -Source (Join-Path $selectorOut $name) -DestinationFolder $app
        }
        Copy-Symbols -SourceFolder $selectorOut -Name 'LowLatencyDriverSelector' -DestinationFolder $symbols

        # Both apps carry these. The install folder holds one copy, so they have to match.
        foreach ($name in $SharedAppFiles) {
            $selectorCopy = Join-Path $selectorOut $name

            if (-not (Test-Path $selectorCopy)) {
                throw "Missing build output: $selectorCopy"
            }

            if ((Get-FileHash $selectorCopy).Hash -ne (Get-FileHash (Join-Path $app $name)).Hash) {
                throw "The ASIO control panel and the Driver Selector carry different copies of $name. They install into one folder, so build both against the same Windows App SDK packages."
            }
        }

        foreach ($pri in @('USBAsioControlPanel.pri', 'LowLatencyDriverSelector.pri')) {
            Assert-XamlInPri -PriPath (Join-Path $app $pri)
        }

        if (Test-Path (Join-Path $app 'resources.pri')) {
            throw 'A resources.pri was staged. Unpackaged apps load resources.pri before their own <exe name>.pri, so both apps would load it.'
        }

        foreach ($binary in @('USBAsio.dll', 'USBAsioControlPanel.exe', 'LowLatencyDriverSelector.exe')) {
            Assert-Machine -Path (Join-Path $app $binary) -BuildPlatform $buildPlatform
        }

        # The driver package: every file in it except symbols.
        $driverSource = Get-DriverSourceFolder -BuildPlatform $buildPlatform

        foreach ($name in @($DriverInfName, $DriverSysName, $DriverCatName)) {
            if (-not (Test-Path (Join-Path $driverSource $name))) {
                throw "Missing driver file: $(Join-Path $driverSource $name)"
            }
        }

        Get-ChildItem $driverSource -File | Where-Object { $_.Extension -ne '.pdb' } | ForEach-Object {
            Copy-Item $_.FullName -Destination $driver -Force
            Write-Detail ('{0,-45} {1:yyyy-MM-dd HH:mm}' -f $_.Name, $_.LastWriteTime)
        }

        Get-ChildItem $driverSource -File -Filter '*.pdb' | Copy-Item -Destination $symbols -Force
        Copy-Symbols -SourceFolder (Get-DriverBuildFolder -BuildPlatform $buildPlatform) -Name ([System.IO.Path]::GetFileNameWithoutExtension($DriverSysName)) -DestinationFolder $symbols

        Assert-Machine -Path (Join-Path $driver $DriverSysName) -BuildPlatform $buildPlatform

        if (Test-AttestationSigned -CatalogPath (Join-Path $driver $DriverCatName)) {
            Write-Detail 'Driver catalog: signed by Microsoft through attestation'
        }
        elseif ($Sign -and -not $AllowTestSignedDriver) {
            throw "The $buildPlatform driver catalog is not signed by $AttestationSigner, so customers' PCs won't load the driver. Pass -AttestedDriverPath with the driver Microsoft signed, or -AllowTestSignedDriver for an internal test build."
        }
        else {
            Write-Warning "The $buildPlatform driver is not attestation-signed. This installer only works on PCs with test signing turned on."
        }

        # The driver package is never signed here: Microsoft's catalog signature covers it, and a
        # second signature on the catalog would replace Microsoft's.
        Invoke-Sign -Path $app.FullName
    }
}

function Get-Dependency {
    param(
        [Parameter(Mandatory)] [string] $Url,
        [Parameter(Mandatory)] [string] $Destination,

        # When set, the download is repeated if the copy on disk is a different version. The
        # version is kept in a file next to it, because the installer's own version doesn't say.
        [string] $Version
    )

    $marker = "$Destination.version"

    $stale = $Version -and (-not (Test-Path $marker) -or (Get-Content $marker -Raw).Trim() -ne $Version)

    if ($RefreshDependencies -or $stale -or -not (Test-Path $Destination)) {
        New-Item (Split-Path $Destination) -ItemType Directory -Force | Out-Null

        $download = "$Destination.download"
        Write-Detail "Downloading $Url"

        Invoke-WebRequest -Uri $Url -OutFile $download -UseBasicParsing
        Move-Item $download $Destination -Force

        if ($Version) {
            Set-Content -Path $marker -Value $Version -Encoding utf8NoBOM
        }
    }

    $signature = Get-AuthenticodeSignature -FilePath $Destination

    if ($signature.Status -ne 'Valid' -or $null -eq $signature.SignerCertificate -or $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation') {
        throw "$Destination is not signed by Microsoft. Delete it and run again."
    }

    Write-Detail ('{0,-45} {1}' -f (Split-Path $Destination -Leaf), (Get-Item $Destination).VersionInfo.ProductVersion)

    return $Destination
}

function Get-WindowsAppSdkVersion {
    $versions = foreach ($config in $AppPackagesConfigs) {
        $package = ([xml](Get-Content $config -Raw)).packages.package |
            Where-Object { $_.id -eq 'Microsoft.WindowsAppSDK' }

        if (-not $package) {
            throw "$config does not reference Microsoft.WindowsAppSDK."
        }

        $package.version
    }

    $unique = @($versions | Sort-Object -Unique)

    if ($unique.Count -ne 1) {
        throw "The ASIO control panel and the Driver Selector use different Windows App SDK versions ($($unique -join ', ')). They install into one folder and share its files, so update both packages.config files together."
    }

    return $unique[0]
}

function Invoke-SetupTarget {
    param([Parameter(Mandatory)] $Version)

    Write-Step 'Setup'

    $windowsAppSdkVersion = Get-WindowsAppSdkVersion
    $windowsAppSdkChannel = ($windowsAppSdkVersion -split '\.')[0..1] -join '.'

    # The x64 Visual C++ runtime package also installs the Arm64 runtime.
    $vcRuntime = Get-Dependency `
        -Url 'https://aka.ms/vc14/vc_redist.x64.exe' `
        -Destination (Join-Path $DependenciesRoot 'VC_redist.x64.exe')

    foreach ($buildPlatform in $Platforms) {
        Write-Detail "--- $buildPlatform ---"

        $runtimeArch = $buildPlatform.ToLowerInvariant()

        $windowsAppRuntime = Get-Dependency `
            -Url "https://aka.ms/windowsappsdk/$windowsAppSdkChannel/$windowsAppSdkVersion/windowsappruntimeinstall-$runtimeArch.exe" `
            -Destination (Join-Path $DependenciesRoot "$runtimeArch\WindowsAppRuntimeInstall-$runtimeArch.exe") `
            -Version $windowsAppSdkVersion

        if (-not (Test-Path (Join-Path $StagingRoot "$buildPlatform\app\USBAsio.dll"))) {
            throw "Nothing is staged for $buildPlatform. Run the Stage target first."
        }

        $properties = Get-VersionProperties -Version $Version
        $properties['LlaStagingFolder'] = $StagingRoot
        $properties['LlaVCRuntimeInstaller'] = $vcRuntime
        $properties['LlaWindowsAppRuntimeInstaller'] = $windowsAppRuntime

        # The package embeds the custom action DLL, so it is built (and, with -Sign, signed) first.
        Invoke-MSBuild -Project $CustomActionsProject -BuildPlatform $buildPlatform -Properties $properties
        Invoke-MSBuild -Project $BundleProject -BuildPlatform $buildPlatform -Properties $properties -Restore

        foreach ($output in @((Get-BundlePath -BuildPlatform $buildPlatform), (Get-PackagePath -BuildPlatform $buildPlatform))) {
            if (-not (Test-Path $output) -or (Get-Item $output).LastWriteTime -lt $RunStarted) {
                throw "The build did not produce a new $output"
            }

            Write-Detail "Built $output"
        }
    }
}

function Get-BundlePath {
    param([Parameter(Mandatory)] [string] $BuildPlatform)

    Join-Path $OutRoot "LowLatencyAudioBundle\$BuildPlatform\$Configuration\LowLatencyAudioSetup.exe"
}

function Get-PackagePath {
    param([Parameter(Mandatory)] [string] $BuildPlatform)

    Join-Path $OutRoot "LowLatencyAudioPackage\$BuildPlatform\$Configuration\LowLatencyAudio.msi"
}

function Invoke-ReleaseTarget {
    param([Parameter(Mandatory)] $Version)

    Write-Step 'Release'

    $releaseFolder = Join-Path $ReleaseRoot $Version.Text
    New-Item $releaseFolder -ItemType Directory -Force | Out-Null

    foreach ($buildPlatform in $Platforms) {
        $bundle = Get-BundlePath -BuildPlatform $buildPlatform
        $package = Get-PackagePath -BuildPlatform $buildPlatform

        foreach ($file in @($bundle, $package)) {
            if (-not (Test-Path $file)) {
                throw "Missing $file. Run the Setup target first."
            }
        }

        $suffix = "$($Version.Text)-$buildPlatform"

        $releasedBundle = Join-Path $releaseFolder "LowLatencyAudioSetup-$suffix.exe"
        $releasedPackage = Join-Path $releaseFolder "LowLatencyAudio-$suffix.msi"

        Copy-Item $bundle $releasedBundle -Force
        Copy-Item $package $releasedPackage -Force

        $symbolsFolder = Join-Path $releaseFolder "symbols\$buildPlatform"
        New-Item $symbolsFolder -ItemType Directory -Force | Out-Null

        $stagedSymbols = Join-Path $StagingRoot "$buildPlatform\symbols"
        if (Test-Path $stagedSymbols) {
            Copy-Item (Join-Path $stagedSymbols '*') -Destination $symbolsFolder -Force
        }

        Copy-Symbols -SourceFolder (Join-Path $OutRoot "LowLatencyAudioSetupActions\$buildPlatform\$Configuration") -Name 'LowLatencyAudioSetupActions' -DestinationFolder $symbolsFolder

        foreach ($file in @($releasedBundle, $releasedPackage)) {
            $signature = (Get-AuthenticodeSignature -FilePath $file).Status
            Write-Detail ('{0}  {1}  signature: {2}' -f (Split-Path $file -Leaf), (Get-FileHash $file).Hash, $signature)
        }
    }

    Write-Detail "Release folder: $releaseFolder"
}

# ----------------------------------------------------------------------------------------------
# Main
# ----------------------------------------------------------------------------------------------

$targets = Expand-Argument -Value $Target -Allowed ($AllTargets + 'All') -Name 'target'
if ($targets -contains 'All') {
    $targets = @($targets | Where-Object { $_ -ne 'All' }) + $DefaultTargets
}

$Platforms = @(Expand-Argument -Value $Platform -Allowed @('x64', 'Arm64') -Name 'platform' |
    ForEach-Object { Get-NormalizedPlatform -Value $_ } | Select-Object -Unique)

if ($AttestedDriverPath) {
    if (-not (Test-Path $AttestedDriverPath -PathType Container)) {
        throw "-AttestedDriverPath does not exist: $AttestedDriverPath"
    }
    $AttestedDriverPath = (Resolve-Path $AttestedDriverPath).Path
}

if ($Sign -and -not $CertificateThumbprint) {
    throw '-Sign needs a certificate. Pass -CertificateThumbprint or set LLA_SIGNING_THUMBPRINT.'
}

# Child processes, including the signing targets MSBuild runs, read the signing settings from
# the environment. They are set or cleared here, and put back afterwards, so a value left in the
# shell can't sign a build that was not asked to be signed.
$savedEnvironment = @{}
foreach ($name in @('LLA_SIGNING_THUMBPRINT', 'LLA_TIMESTAMP_URL', 'LLA_SIGNING_DESCRIPTION')) {
    $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name)
}

$process = Get-Process -Id $PID
$savedPriority = $process.PriorityClass

try {
    $process.PriorityClass = $Priority

    if ($Sign) {
        $env:LLA_SIGNING_THUMBPRINT = $CertificateThumbprint
        $env:LLA_TIMESTAMP_URL = $TimestampUrl
        $env:LLA_SIGNING_DESCRIPTION = Get-BrandingValue -Name 'ProductName'
    }
    else {
        Remove-Item Env:LLA_SIGNING_THUMBPRINT -ErrorAction SilentlyContinue
        Remove-Item Env:LLA_TIMESTAMP_URL -ErrorAction SilentlyContinue
        Remove-Item Env:LLA_SIGNING_DESCRIPTION -ErrorAction SilentlyContinue
    }

    if ($BumpBuildNumber) {
        Step-BuildNumber
    }

    $version = Get-ReleaseVersion

    Write-Host "Low-Latency Audio $($version.Text) ($($version.Numeric)), $Configuration, $($Platforms -join ' and ')" -ForegroundColor White
    Write-Host "Targets: $(($AllTargets | Where-Object { $targets -contains $_ }) -join ', ')$(if ($Sign) { ', signed' })" -ForegroundColor White

    $needsMSBuild = @('Driver', 'Asio', 'Apps', 'Setup') | Where-Object { $targets -contains $_ }
    if ($needsMSBuild) {
        $script:MSBuild = Resolve-MSBuild
        Write-Detail "MSBuild: $script:MSBuild"
    }

    if ($targets -contains 'Stage') {
        $script:MakePri = Resolve-WindowsKitTool -Name 'makepri.exe'
    }

    foreach ($name in $AllTargets) {
        if ($targets -notcontains $name) {
            continue
        }

        switch ($name) {
            'Clean' { Invoke-CleanTarget }
            'Driver' { Invoke-DriverTarget }
            'DriverCab' { Invoke-DriverCabTarget -Version $version }
            'Asio' { Invoke-AsioTarget }
            'Apps' { Invoke-AppsTarget -Version $version }
            'Stage' { Invoke-StageTarget }
            'Setup' { Invoke-SetupTarget -Version $version }
            'Release' { Invoke-ReleaseTarget -Version $version }
        }
    }

    Write-Host ''
    Write-Host ('Done in {0:mm\:ss}' -f ((Get-Date) - $RunStarted)) -ForegroundColor Green
}
finally {
    foreach ($name in $savedEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name])
    }

    $process.PriorityClass = $savedPriority
}
