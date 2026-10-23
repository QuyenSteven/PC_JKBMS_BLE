# JK BMS Studio — unified Windows application

Native C++20 Bluetooth LE engine + embedded WebView2 HTML/CSS/JavaScript dashboard (no Python, no Win32 dashboard widgets).

## Architecture

- `src/main.cpp`: Win32 WebView2 host, BLE worker, GATT notifications, frame assembler and verified read-only telemetry.
- `web/index.html`: complete responsive visual UI and message bridge to native backend.
- `JK_BMS_PC.vcxproj`: VS2026 v145 / VS2022 v143; automatically copies `web/index.html` beside the EXE.
- `.github/workflows/windows-build.yml`: Windows build CI.

## Build / run

1. `git pull origin main`.
2. Open `JK_BMS_PC.sln` in Visual Studio 2026.
3. Restore NuGet packages: `Microsoft.Windows.CppWinRT` and `Microsoft.Web.WebView2`.
4. Select **Release | x64**, then **Rebuild Solution**.
5. WebView2 Runtime must be installed (normally present with Microsoft Edge).
6. Run EXE, scan for a BLE device, choose it, then Connect. Watch **BLE / Nhật ký**.

## JK protocol

JK BLE characteristic `FFE1` under service `FFE0`.
After BLE notifications subscribe, `0x96` asks for Settings (response `0x01`), `0x97` asks for Device Info (response `0x03`); live measurements arrive as `0x02` streaming frames. No fake data, no repeated `0x96` workaround.

Decoder for `JK02_32S` is experimental and MUST be verified against actual device logs. `JK02_24S` and `JK04` are displayed as protocol options but do **not** yet decode verified telemetry. Connection log, scan and raw capture are available.

## Critical limitations

This is a **new architectural baseline, not a tested production build**. No Windows build verification or physical BMS validation is available here. Protection writes, MOSFET/balancing switches, configuration restore and reboot are intentionally disabled until exact model, firmware and write-acknowledgment semantics can be checked.

To debug missing measurements: capture RAW BLE logs and inspect whether valid frame `0x02` arrives. `0x01` and `0x03` alone are insufficient for pack voltage, current and SOC.
