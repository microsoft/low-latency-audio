# Low-Latency Audio Driver Selector

The Driver Selector is a small app that lets a customer choose which driver each USB Audio 2.0 device uses: the low-latency driver from this project, the Windows USB Audio 2.0 driver, or a driver from the device's manufacturer. It ships in the Low-Latency Audio installer, with a shortcut in the Start menu.

## Why it exists

The low-latency driver's INF doesn't list any devices. That keeps Windows from putting the driver on a device by itself, so nothing about a customer's PC changes until they ask for it. The Driver Selector is how they ask, and how they go back.

The driver supports USB Audio 2.0 only, so the app lists only USB Audio 2.0 devices. Devices that use USB Audio 1.0, such as the microphone in many webcams, aren't listed.

## What the customer sees

Each USB Audio 2.0 device gets a row with its name, the driver it uses now and that driver's version, and up to three buttons:

- **Use low-latency driver** puts the newest copy of the low-latency driver on the device's audio interface. Music apps can then use the device through ASIO.
- **Use Windows driver** puts the Windows USB Audio 2.0 driver back. When a manufacturer's driver controls the whole device, the whole device goes back to Windows' own drivers.
- **Use manufacturer's driver** appears when the manufacturer's driver for the device is installed on the PC.

A button is turned off when pressing it wouldn't change anything. Before any change, the app asks the customer to confirm, and tells them to close apps that are using the device.

After a change, the app checks whether the device really restarted on the new driver. If it didn't, it asks the customer to unplug the device and plug it in again, or to restart Windows. It never restarts Windows without asking.

If a newer copy of the low-latency driver is installed than the one a device uses, the row says so. Choosing the low-latency driver again moves the device to the newer copy.

## Administrator rights

Changing a driver needs administrator rights, so the app asks for them once, when it starts. If the customer says no, the app still shows which driver each device uses, with a banner and a **Restart as administrator** button.

Command line switches:

| Switch | What it does |
| --- | --- |
| `--noelevate` | Start without asking for administrator rights. Useful for testing the read-only view. |
| `--relaunched` | Used by the app itself when it starts the elevated copy, so a declined prompt can't start a loop. |

## How it works

The low-latency driver is installed the way Device Manager's **Have disk** button does it with **Show compatible hardware** turned off: Windows builds a driver list from that one INF for the device's setup class, and the app installs the driver from that list. The INF doesn't have to name the device.

Most manufacturers' drivers take over the whole USB device instead of only its audio interface. When that's the case, Windows never creates a separate device for the audio interface. To use the low-latency driver on such a device, the app first hands the whole device back to the Windows composite driver, waits up to 30 seconds for the audio interface to appear, and then puts the low-latency driver on it.

A USB Audio 2.0 interface is recognized by the compatible ID Windows gives it, `USB\Class_01&SubClass_00&Prot_20`. A USB Audio 1.0 interface gets a different one. A device a manufacturer's driver has taken over whole has no such interface, so for those the app reads the device's configuration descriptor through the USB hub it's plugged into, and lists the device only if it has a USB Audio 2.0 function. That read doesn't need administrator rights.

The app finds the low-latency driver in the driver store by the INF file name it was built with, `USBAudio2-ACX.inf`. If that name or the driver's service name changes, change them in [SelectorConfig.h](SelectorConfig.h).

| File | What's in it |
| --- | --- |
| [DriverTools.cpp](DriverTools.cpp) | Finding the devices, describing their drivers, and changing them |
| [../shared/driver-store/DriverStore.cpp](../shared/driver-store/DriverStore.cpp) | Finding driver packages in the driver store, and installing a driver from one INF on one device. The installer's custom actions use it too. |
| [MainWindow.xaml](MainWindow.xaml) and [MainWindow.xaml.cpp](MainWindow.xaml.cpp) | The window |
| [Strings/en-US/Resources.resw](Strings/en-US/Resources.resw) | Every string the customer sees |
| [SelectorConfig.h](SelectorConfig.h) | The driver's INF and service names, and the switch for the PREVIEW badge |

Device work runs on a background thread. Changing a driver can take many seconds while the device restarts, and none of it runs on the window's thread.

The app writes ETW events to the provider `Microsoft.LowLatencyAudio.DriverSelector`, `{0b9e5cc5-9731-5b16-7820-790338edd8af}`. Nothing is sent anywhere. Window size and position are saved in `HKEY_CURRENT_USER\Software\Microsoft\Low-Latency Audio\Driver Selector`.

## Building

Open [LowLatencyDriverSelector.sln](LowLatencyDriverSelector.sln) in Visual Studio 2022 or later and build x64 or ARM64. NuGet restores the packages the first time. To build it the way a release does, run `build\build-all.ps1 -Target Apps` from the repository root.

The app installs into the same folder as the ASIO control panel. That works because:

- both apps use the same Windows App SDK packages, so the `Microsoft.WindowsAppRuntime.Bootstrap.dll` and `Microsoft.Web.WebView2.Core.dll` they each carry are the same file. Update `packages.config` in both projects together. `build-all.ps1` stops if the two copies differ.
- each app keeps its strings and its compiled XAML inside its own `<exe name>.pri`. Neither may produce a `resources.pri`, because both apps would load it. `build-all.ps1` checks this too.

## Known limitations

- Only devices that are plugged in are listed.
- A device that offers USB Audio 2.0 only in its second configuration isn't listed, because Windows' composite driver uses the first one.
- Windows keeps the driver choice per device and USB port. A device without a serial number that's plugged into a different port shows up as a new device on the Windows driver. Choose the low-latency driver for it again.
- If Windows Update later offers a driver that names the device, Windows can replace the low-latency driver with it.
- If switching straight from a manufacturer's driver to the low-latency driver fails, switch to the Windows driver first, then to the low-latency driver. A manufacturer's driver that installs on the audio interface under a setup class other than Media can't be swapped directly.
- The Driver Selector doesn't move a device back to the manufacturer's driver after the low-latency driver is uninstalled. Uninstalling moves devices to the Windows driver.
