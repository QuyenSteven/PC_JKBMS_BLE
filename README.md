# JK BMS Studio — Native C++20 / Windows BLE

Windows 10/11 desktop application, Visual Studio 2026 (v145) or Visual Studio 2022 (v143). No Python.

## How to run

1. Open `JK_BMS_PC.sln` in Visual Studio.
2. Select **Release | x64** and **Build → Rebuild Solution**.
3. Run. Click **Scan BLE**, select the JK BMS, then **Connect**.
4. Choose the correct protocol from the dropdown.

## Modern dashboard (work in progress)

- Dark layout with Overview, Cells, Control, Protection, BLE Logs.
- Live summary: pack voltage, current, power and SOC.
- Cell voltage bar chart, minimum/maximum and delta.
- Charge MOSFET, discharge MOSFET and balance readouts.
- BLE scan, connect/disconnect, FFE0/FFE1 notifications, Save RAW.
- JK02_32S, JK02_24S and JK04 **experimental** decoding.

## Important safety limitations

**Read-only build.** Control and protection pages do not yet write configuration. Reboot BMS is not implemented. The settings and model-specific decoders have not been validated on real hardware, so compatibility with every JK BMS model cannot be claimed.

The Windows CI workflow builds the project on a Windows runner and uploads an EXE artifact after successful compilation. Actual hardware connection and protocol correctness must be checked on the target BMS.

See `.github/workflows/windows-build.yml` for CI.