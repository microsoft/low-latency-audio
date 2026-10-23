# Low-Latency Audio installer

This folder builds the installer that puts the low-latency USB Audio 2.0 driver, the ASIO driver, the ASIO control panel and the Low-Latency Audio Driver Selector on a customer's PC. There is one installer for x64 PCs and one for Arm64 PCs. The driver supports USB Audio 2.0 devices only, not USB Audio 1.0.

The older installer in `src\installer` is for developers and is left as it is. This installer removes an installed copy of it.

## What it installs

Each installer is a bundle, `LowLatencyAudioSetup.exe`. It runs three packages in order:

1. **The Visual C++ runtime.** The two apps need it. The ASIO driver and the installer's own custom actions link the runtime in, so they don't.
2. **The Windows App Runtime**, the version the two apps are built against.
3. **`LowLatencyAudio.msi`**, which installs:

| What | Where |
| --- | --- |
| `USBAsio.dll`, the ASIO driver, and the registry entries music apps find it by | `Program Files\Low-Latency Audio` |
| `USBAsioControlPanel.exe` and `.pri`, plus its `Assets` folder | the same folder, because the ASIO driver starts the control panel from there |
| `LowLatencyDriverSelector.exe` and `.pri` | the same folder |
| `Microsoft.WindowsAppRuntime.Bootstrap.dll` and `Microsoft.Web.WebView2.Core.dll`, shared by both apps | the same folder |
| The driver package: `USBAudio2-ACX.inf`, `.sys` and `.cat` | `Program Files\Low-Latency Audio\Driver`, and the Windows driver store |
| Start menu shortcuts for the Driver Selector and the ASIO control panel | a Start menu folder |

The folder and shortcut names come from [low-latency-audio-installer/Branding.wxi](low-latency-audio-installer/Branding.wxi).

The finish page offers to open the Driver Selector, because that's where the customer chooses which devices use the new driver.

## The driver package

The driver's INF doesn't list any devices, so installing it changes nothing about the customer's devices. The installer adds the package to the driver store, the same as `pnputil /add-driver` without `/install`, and stops there. The customer then picks devices in the Driver Selector.

Two custom actions in [low-latency-audio-installer/custom-actions](low-latency-audio-installer/custom-actions) do the work. They're a small C++ DLL that shares its driver store code with the Driver Selector.

| When | What happens |
| --- | --- |
| Install | The package is added to the driver store. |
| Upgrade | The new package is added. Every device that used an older copy of the driver moves to the new one, and older copies that nothing uses anymore are deleted. |
| Uninstall | Every device using the driver moves to the next best driver, which for a USB Audio 2.0 interface is the Windows driver. Then every copy of the package is deleted from the driver store. |

"A copy of the driver" means a package built from an INF named `USBAudio2-ACX.inf` with the same `Provider` as the one being installed. **Keep the INF's `Provider` the same in every release**, or an upgrade won't find the copies older releases installed.

The uninstall action doesn't run while an upgrade removes the older release, so devices don't drop back to the Windows driver in the middle of an upgrade.

Neither action has a rollback action. If an install fails after the package was added, the package stays in the driver store. Running the installer again and then uninstalling removes it.

If moving a device or deleting a package needs Windows to restart, the installer asks for a restart at the end.

## The ASIO driver registration

The installer writes the same registry entries `USBAsio.dll` writes when it registers itself, instead of calling it to register itself. That way the entries come and go with the file. They are:

- `HKLM\SOFTWARE\Classes\CLSID\{327468A4-1351-4930-BB6B-0FEB69BF5D70}`, with `InprocServer32` pointing at `USBAsio.dll` and `ThreadingModel` set to `Apartment`
- `HKLM\SOFTWARE\ASIO\<name>`, with `CLSID` and `Description`

The name is `USB ASIO` on x64 and `USB ASIO (ARM64X)` on Arm64. The Arm64 installer carries the Arm64X build of the DLL, so both x64 and Arm64 music apps can load it. If the ASIO driver's name or class ID changes in `src\uac2-asio\USBAsio.cpp`, change [Branding.wxi](low-latency-audio-installer/Branding.wxi) to match.

There is no 32-bit ASIO driver, so 32-bit music apps don't see it.

## Checks before installing

The bundle and the .msi both refuse to install:

- on a PC with the other processor type. An x64 installer runs on an Arm64 PC through emulation, and the x64 driver would never load there.
- on Windows older than the build in `MinimumWindowsBuild` in [Branding.wxi](low-latency-audio-installer/Branding.wxi). It's 22621, Windows 11 version 22H2, because the driver uses ACX 1.1 and KMDF 1.33.

## Branding

Everything a release partner sets is in [Branding.wxi](low-latency-audio-installer/Branding.wxi): the publisher, the product name, the folder and shortcut names, the license and support links, the upgrade codes, and the minimum Windows build. Values marked PLACEHOLDER have to be confirmed before a release goes to customers. Values marked KEEP must never change after the first release, or upgrades stop working.

The icon is [src/driver-selector/Assets/AppIcon.ico](../driver-selector/Assets/AppIcon.ico), and the logo on the installer's window is [bundle/logo.png](low-latency-audio-installer/bundle/logo.png). Both are placeholders.

## Building a release

Use [build/build-all.ps1](../../build/build-all.ps1) from PowerShell 7. It builds every project, stages what the installer carries, signs it, builds the installers and collects the release. The version comes from [build/version.json](../../build/version.json).

```powershell
cd build
.\build-all.ps1 -Target Apps,Stage,Setup -Platform x64
```

| Target | What it does |
| --- | --- |
| `Clean` | Deletes the staging folder and the output of every project the script builds |
| `Driver` | Builds the driver. Needs the Windows Driver Kit. |
| `DriverCab` | Puts each platform's driver into a .cab to submit for attestation signing |
| `Asio` | Builds the ASIO driver for x64, and for Arm64EC, which produces the Arm64X DLL. Needs the Steinberg ASIO SDK in `src\uac2-asio\asio`. |
| `Apps` | Builds the ASIO control panel and the Driver Selector |
| `Stage` | Copies everything into `build\staging\installer\<platform>` and checks it |
| `Setup` | Downloads the two runtime installers if needed, then builds the custom actions, the .msi and the bundle |
| `Release` | Copies the installers and symbols into `build\release\<version>` |
| `All` | `Driver`, `Asio`, `Apps`, `Stage`, `Setup`, `Release` |

While it stages, the script checks that:

- both apps carry the same copy of each file they share
- each app's compiled XAML is inside its own .pri file, and there is no `resources.pri`
- every binary is built for the platform it ships on
- the driver's catalog is signed by Microsoft through attestation signing. Without that, it warns that the installer only works on PCs with test signing turned on, and a signed build stops.

### The driver has to be signed by Microsoft

Windows only loads a kernel driver that Microsoft signed. For a preview, that's attestation signing in Partner Center, which needs an EV code signing certificate. It's a manual step in the middle of a release:

1. Build the driver and the .cab files, signed with the EV certificate:

   ```powershell
   .\build-all.ps1 -Target Driver,DriverCab -Sign -CertificateThumbprint <thumbprint>
   ```

   The .cab files are in `build\release\<version>\attestation`, one per platform.

2. Submit each .cab in Partner Center for attestation signing. Download the signed results and unzip them all into one folder. Each one unzips into a folder named for its platform, `x64` or `ARM64`.

3. Build everything else around the signed driver:

   ```powershell
   .\build-all.ps1 -Target Asio,Apps,Stage,Setup,Release -Sign -CertificateThumbprint <thumbprint> -AttestedDriverPath <folder>
   ```

Attestation-signed drivers load on Windows 10 and Windows 11 desktop editions only.

## Signing

`-Sign` signs the attestation .cab files, the staged binaries, the custom action DLL, the .msi, and the bundle. The signing itself is in [build/sign-files.ps1](../../build/sign-files.ps1), which runs `signtool` with the certificate whose thumbprint you pass. The installer projects call the same script through [Directory.Build.targets](Directory.Build.targets).

- The certificate has to be in your certificate store with its private key. An EV certificate on a hardware token works: plug in the token and enter its password when signtool asks. A token that supports single logon asks once instead of for every file.
- Pass the thumbprint with `-CertificateThumbprint`, or set `LLA_SIGNING_THUMBPRINT`.
- Every signature is timestamped. The default timestamp server is DigiCert's. Use `-TimestampUrl`, or set `LLA_TIMESTAMP_URL`, to use your certificate vendor's.
- Files that already have a valid signature, such as Microsoft's runtime files and the attestation-signed driver, keep it.

The custom action DLL is signed separately from the .msi on purpose. Windows Installer unpacks it to a temporary folder and loads it, the .msi's signature doesn't cover it there, and Smart App Control blocks it if it's unsigned. The install then fails with error 1723.

To try the signing steps without the real certificate, make a test certificate, sign with it, and delete it afterwards:

```powershell
$cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=Signing test' -CertStoreLocation Cert:\CurrentUser\My
.\build-all.ps1 -Target Stage,Setup -Platform x64 -Sign -CertificateThumbprint $cert.Thumbprint -AllowTestSignedDriver
Remove-Item "Cert:\CurrentUser\My\$($cert.Thumbprint)" -DeleteKey
```

The signatures show as untrusted, which is expected for a test certificate.

## Testing an installer

Install with a log, then search it for `LowLatencyAudioSetup:`. Those lines come from the custom actions and say which package was added, moved or removed.

```powershell
msiexec /i LowLatencyAudio-<version>-x64.msi /l*v install.log
pnputil /enum-drivers
```

`pnputil /enum-drivers` lists the package as `oemNN.inf` with the original name `USBAudio2-ACX.inf`. After uninstalling, it shouldn't be listed anymore.

## Visual Studio

[low-latency-audio-installer/LowLatencyAudioInstaller.sln](low-latency-audio-installer/LowLatencyAudioInstaller.sln) holds the custom actions, the package and the bundle. It builds from whatever is in `build\staging\installer`, so run `build-all.ps1 -Target Stage` first. The bundle needs the runtime installers too, which `build-all.ps1 -Target Setup` downloads into `build\dependencies`.
