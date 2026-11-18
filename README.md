# Windows 平板机身按钮自定义

Windows 11 原生 C++ 平板实体按键绑定工具，提供 ARM64 和 x64 构建配置。

[English documentation](README.en.md)

## 已验证的方案

在一台 Surface Pro X 上完成了以下实机验证：

1. Raw Input 可枚举 Surface Button VHF HID，部分事件有延迟或缺失。
2. 共享只读 ReadFile 配合 HidP_GetUsagesEx 能收到完整的电源、音量加减按下和释放。
3. 实体音量键未进入低级键盘钩子的 VK_VOLUME_UP/DOWN 路径。
4. 单独禁用特定 ButtonConverter Consumer Control 子设备后，机身停止调节音量，外接键盘仍可调节，应用仍可识别机身按键。
5. 按住电源时自动暂停该子设备，全部按键释放后恢复的版本，已由该设备用户测试通过。

## 功能

- 电源、音量加、音量减的单击、双击、三击与长按绑定。
- 电源与音量、双音量组合。
- 启动应用、设置页面、截图等内置动作。
- 首次运行扫描 Consumer Control 候选并保存当前用户配置。
- 使用管理员辅助进程切换设备，主界面继续以普通权限运行。

## 开始使用

安装 Windows PowerShell PnpDevice 模块可用的 Windows 11 环境。先构建，或将对应架构程序放在 Start.ps1 同目录。

    powershell -NoProfile -ExecutionPolicy Bypass -File .\Start.ps1

在设备设置中扫描并选择经过实际禁用/恢复测试的 Consumer 子设备。测试应确认机身音量停止变化、外接键盘音量正常、HID 按键识别仍正常。保存后启动应用并启用手势绑定和电源自动切换。

设备实例路径属于本机配置，不可跨机器照抄。需要重新设置时运行 DeviceSetup.ps1，保存后重启应用。

## 构建

Visual Studio 2022/2026，安装 C++ 桌面开发、Windows 11 SDK 和目标架构 MSVC 工具。打开 SurfaceButtonBindings.sln，选择 Release | ARM64 或 Release | x64。

    msbuild SurfaceButtonBindings.sln /p:Configuration=Release /p:Platform=ARM64

默认输出为 bin\ARM64\Release。构建后的程序命名应为 SurfaceButtonBindings-ARM64.exe 或 SurfaceButtonBindings-x64.exe，以便 Start.ps1 启动。

## 事件规则

短按释放后等待 350 ms 确认单击或双击，第三击释放立即触发。长按至少 500 ms，释放时触发。组合在重叠状态出现时触发，参与组合的单键释放不再触发单键动作。

电源按下请求暂停音量子设备，电源和音量全部释放后恢复。最长暂停 10 秒。主进程结束时辅助进程请求恢复。设备切换有耗时，音量先于电源按下时仍可能调音量。电源短按需自行在 Windows 中设为不采取任何操作。

## 兼容范围与限制

Surface Pro X 的固定设备版本已实测成功。当前配置版 ARM64/x64 已构建，跨机型流程尚待实测。输入读取当前选择首个可打开的 0001:000D Portable Device Control 集合，使用 HID parser 解析标准电源、音量 usage。私有 usage、其他 TLC、多报告 ID 状态合并和多候选输入选择需要继续适配。

设置向导提供扫描和保存，候选禁用/恢复验证目前由用户手动执行。它尚未自动验证、迁移或恢复失效配置。暂停功能作用于完整子设备。退出或崩溃后的恢复依赖独立辅助进程继续运行，恢复失败时应以管理员身份对保存的设备实例执行 pnputil /enable-device。

## 隐私

配置保存于 HKCU\Software\SurfaceButtonBindings。仓库不包含个人设备实例、按键日志或上传的诊断文本。程序本身不发送遥测数据。

