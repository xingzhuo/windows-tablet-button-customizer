# Windows Tablet Button Customizer

Customize physical power and volume buttons on Windows 11 tablets. Native C++ with ARM64 and x64 Visual Studio configurations.

[中文说明](README.md)

## How the solution works

The application reads HID input reports through a shared, read-only handle. Windows HID parser APIs decode standard power and volume usages. A gesture recognizer maps button transitions to single, double and triple clicks, long presses, and combinations.

On the tested Surface Pro X, a separate Consumer Control child device handles system volume adjustment. Temporarily disabling that specific child device stops the tablet buttons from changing system volume while the input collection used by this application continues to deliver button reports. External keyboard volume controls continued to work in the user's tests.

An elevated helper process disables the configured child device when the power button is held. It requests restoration after power and volume buttons are all released. The main UI stays responsive. The helper also requests restoration when the main process exits. A ten-second limit bounds each pause period. Firmware power behavior and the Windows power-button policy must be considered when testing: set the normal short-press power action to Do nothing to keep the application running.

## Evidence and compatibility

The user tested physical power and volume button transitions, combinations, and long presses on one Surface Pro X running Windows 11 ARM64. Shared HID reads delivered repeated press/release reports where Raw Input was delayed or incomplete. A low-level keyboard hook received no corresponding volume-key events.

Disabling one ButtonConverter Consumer Control instance stopped tablet volume changes while external keyboard volume and custom bindings remained functional. The automatic power-held switching version was subsequently reported to work well.

ARM64 and x64 Release configurations have been built. The current device-configuration version has not been validated across different tablet models. The tested instance ID is intentionally excluded from the public project because instance IDs are machine-specific.

## First run and device setup

Build the application and place the appropriate executable next to Start.ps1, named SurfaceButtonBindings-ARM64.exe or SurfaceButtonBindings-x64.exe.

    powershell -NoProfile -ExecutionPolicy Bypass -File .\Start.ps1

On first launch, DeviceSetup.ps1 opens a graphical setup window. Scan for Consumer Control candidates. Select a device only after testing that disabling it stops tablet volume changes, preserves external keyboard volume, and preserves button recognition by the application. The wizard asks for confirmation of that test before saving.

The wizard currently scans and saves a selection; the disable/restore validation itself is manual. Windows PowerShell must provide the PnpDevice module. Run DeviceSetup.ps1 again to change the profile, then restart the application.

The profile is stored under HKCU\Software\SurfaceButtonBindings. It includes the target instance ID, device name, and machine model when available. Start.ps1 checks whether the saved instance is present and opens setup if it is missing. Automatic migration of stale device paths remains future work.

## Bindings and timing

The UI offers separate single-click, double-click, triple-click and long-press bindings for power, volume up and volume down. It also supports power plus either volume key and both volume keys together.

A short release starts a 350 ms click window. Single and double clicks are dispatched when that window expires; the third short release dispatches a triple click immediately. A press lasting at least 500 ms is classified as a long press on release. Buttons that participated in a combination do not subsequently dispatch individual release actions.

Built-in actions include launching Notepad, Calculator, Explorer, Task Manager, the on-screen keyboard, Magnifier, screen clipping, and Windows settings pages. Bindings are stored per user. Enable bindings explicitly after launching or reconnecting.

Enable automatic volume pausing and accept the administrator prompt once per helper session. Try holding power until the UI reports that volume is paused, then press a volume button. Test faster combinations afterward. Device changes take time; volume pressed before the disable operation completes can still change the system volume.

## Build

Use Visual Studio 2022 or 2026 with Desktop development with C++, a Windows 11 SDK, and MSVC tools and libraries for the target architecture. Open SurfaceButtonBindings.sln and build Release | ARM64 or Release | x64. The project uses v143 for VS 2022 and v145 for VS 2026.

    msbuild SurfaceButtonBindings.sln /p:Configuration=Release /p:Platform=ARM64

Default output is bin\ARM64\Release. Rename or copy the executable as described above for the launcher.

## Current limitations and recovery

Input discovery currently opens the first accessible Portable Device Control collection with usage page 0001 and usage 000D. Report lengths come from HID capabilities and standard usages are decoded with HidP_GetUsagesEx. Devices with private usages or other top-level collections need additional adapters. Selection among multiple input candidates and per-report-ID state merging need further work.

The pause operates on the entire configured child device. It cannot selectively suppress only one click pattern. Recovery depends on the helper remaining alive and Windows accepting the enable operation. If necessary, open an elevated terminal and run the following with the saved instance ID:

    pnputil /enable-device "YOUR_DEVICE_INSTANCE_ID"

The source retains code from earlier diagnostic iterations, including unused keyboard-hook paths. The project is an experimental utility and needs further UI and hardware compatibility testing.

## Privacy and license

The application sends no telemetry. Public source excludes personal diagnostic logs, attachments and device instance IDs. MIT license; see LICENSE.
