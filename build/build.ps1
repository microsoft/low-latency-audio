Write-Host "This must be run in a command prompt with MSBuild and other tools paths set. For example, the Developer Command Prompt for VS2022"

$repoRoot = "G:\Github\microsoft\low-latency-audio\"
$sourceRoot = $repoRoot + "src\"
$vsfilesFolder = $sourceRoot + "vsfiles\"
$vsfilesFolderOut = $vsfilesFolder + "out\"
$stagingFolder = $repoRoot + "build\staging\"
$releaseFolder = $repoRoot + "build\release\"
$installerProjectFolder = $sourceRoot + "installer\"

$asioSolution = $sourceRoot + "uac2-asio\USBAsio.sln"
$acxSolution =  $sourceRoot + "uac2-driver\USBAudioAcxDriver.sln"
$controlPanelSolution =  $sourceRoot + "asio-control-panel\USBAsioControlPanel.sln"
$installerProject = $installerProjectFolder + "asio-installer.sln"


#$configurations = ("Debug", "Release")
$configurations = ("Release")



Write-Host "Creating folders..."
New-Item -Path $stagingFolder -ItemType Directory -Force
New-Item -Path $vsfilesFolder -ItemType Directory -Force
New-Item -Path $releaseFolder -ItemType Directory -Force


Write-Host "Cleaning folders..."
Remove-Item "$stagingFolder*" -Recurse -Force
Remove-Item "$vsfilesFolder*" -Recurse -Force
Remove-Item "$releaseFolder*" -Recurse -Force


foreach($configuration in $configurations)
{
    # Build ACX Driver for x64 and Arm64

    msbuild.exe -t:restore $acxSolution -p:RestorePackagesConfig=true

    foreach($acxPlatform in ("x64", "Arm64"))
    {
        Write-Host "Building ACX Driver: $configuration|$acxPlatform"
        msbuild.exe -p:Platform=$acxPlatform -p:Configuration=$configuration -verbosity:normal -target:Rebuild $acxSolution
        if ($LASTEXITCODE -ne 0)
        {
            Write-Host "MSBuild failed for $configuration $acxPlatform build. Exit code $LASTEXITCODE"
            exit;
        }

        # this is where the driver files (cat, sys, inf) are output to
        $driverOutputFolder = "$vsfilesFolderOut\USBAudioAcxDriver\$acxPlatform\$configuration\USBAudioAcxDriver\"
        Write-Host $driverOutputFolder

        # ensure the destination folders exist
        $stagingTargetFolder = "$stagingFolder\$acxPlatform\$configuration\"
        New-Item -Path $stagingTargetFolder -ItemType Directory

        # copy output files to staging
        Copy-Item -Path "$driverOutputFolder\*.*" -Destination $stagingTargetFolder

        Write-Host
    }
    Write-Host

    # build ASIO driver for x64 and Arm64EC

    msbuild.exe -t:restore $asioSolution -p:RestorePackagesConfig=true

    foreach($asioPlatform in ("x64", "Arm64EC"))
    {
        Write-Host "Building ASIO Driver: $configuration|$asioPlatform"
        msbuild.exe -p:Platform=$asioPlatform -p:Configuration=$configuration -verbosity:normal -target:Rebuild $asioSolution 
        if ($LASTEXITCODE -ne 0)
        {
            Write-Host "MSBuild failed for $configuration $asioPlatform build. Exit code $LASTEXITCODE"
            exit;
        }

        # this is where the ASIO files (dll, pdb) are output to
        $asioOutputFolder = "$vsfilesFolderOut\USBAsio\$asioPlatform\$configuration\"
        Write-Host $asioOutputFolder

        # ensure the destination folders exist. We need to map the destination folder
        # because Arm64X puts the output in Arm64EC output folder

        if ($asioPlatform -eq "Arm64EC")
        {
            $destinationAsioPlatform = "Arm64"
        }
        else 
        {
            $destinationAsioPlatform = "x64"
        }

        $stagingTargetFolder = "$stagingFolder\$destinationAsioPlatform\$configuration\"
        #New-Item -Path $stagingTargetFolder -ItemType Directory

            # copy output files to staging
        Copy-Item -Path "$asioOutputFolder*.dll" -Destination $stagingTargetFolder
        Copy-Item -Path "$asioOutputFolder*.pdb" -Destination $stagingTargetFolder

        Write-Host
    }
    Write-Host

    # build ASIO Control Panel dialog for x64 and Arm64

    msbuild.exe -t:restore $controlPanelSolution -p:RestorePackagesConfig=true

    foreach($controlPanelPlatform in ("x64", "Arm64"))
    {
        Write-Host "Building Control Panel:  $configuration|$controlPanelPlatform"
        msbuild.exe -p:Platform=$controlPanelPlatform -p:Configuration=$configuration -verbosity:normal -target:Rebuild $controlPanelSolution

        # this is where the ASIO files (dll, pdb) are output to
        $controlPanelOutputFolder = "$vsfilesFolderOut\USBAsioControlPanel\$controlPanelPlatform\$configuration\"
        Write-Host $controlPanelOutputFolder

        # ensure the destination folders exist. We need to map the destination folder
        # because Arm64X puts the output in Arm64EC output folder

        $stagingTargetFolder = "$stagingFolder\$controlPanelPlatform\$configuration\"

        #New-Item -Path $stagingTargetFolder -ItemType Directory

            # copy output files to staging
        Copy-Item -Path "$controlPanelOutputFolder*.exe" -Destination $stagingTargetFolder

        Write-Host

        #Copy-Item -Path "$sourceRoot\USBAsioControlPanel\USBAsioControlPanel.exe" -Destination $stagingTargetFolder
    }

    # build installers
    Write-Host "Building installers..."

    foreach($installerPlatform in ("x64", "Arm64"))
    {
        msbuild.exe -p:Platform=$installerPlatform -p:Configuration=$configuration -verbosity:normal -target:Rebuild $installerProject 

        if ($LASTEXITCODE -ne 0)
        {
            Write-Host "MSBuild failed for $configuration $installerPlatform installer build. Exit code $LASTEXITCODE"
            exit;
        }

        $releaseTargetFolder = "$releaseFolder\$installerPlatform\$configuration\"
        New-Item -Path $releaseTargetFolder -ItemType Directory

        Copy-Item -Path "$installerProjectFolder\bin\$installerPlatform\$configuration\*.msi" -Destination "$releaseTargetFolder"
    }

}




