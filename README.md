# JK BMS PC Studio — C++/WinRT (Windows)

Native C++20 Win32 desktop application using Windows BLE APIs. No Python, pip or PyInstaller.

## Open in Visual Studio
1. Install Visual Studio 2022 with **Desktop development with C++**, Windows 10/11 SDK and NuGet support.
2. Open `JK_BMS_PC.vcxproj`.
3. Choose **Release | x64**, restore NuGet packages, and Build.
4. Enable Windows Bluetooth, start the app, click **Scan BLE**, choose the JK device and **Connect**.

## Features in this initial version
- Scan BLE device advertisements and connect to JK BMS GATT service `FFE0` characteristic `FFE1`.
- Request device information and configuration, collect notifications and export raw BLE data.
- **Experimental** live telemetry decoding for JK02_32S, JK02_24S and JK04. These decoders have NOT been validated against live devices.
- CI workflow for Windows x64 build, artifact uploaded in Actions after a successful run.

## Safety and incomplete features
This is an experimental **read-only** implementation. Protection parameter writes, charge/discharge MOSFET controls, balancing controls, configuration restore and BMS reboot are **not implemented or verified**. Do not use this version to change safety-critical battery settings.

Protocol layouts and capabilities differ by model and firmware. Verify against real JK BMS hardware before considering full compatibility.
