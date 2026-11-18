#include <windows.h>
#include <shellapi.h>
extern "C" {
#include <hidsdi.h>
}
#include <array>
#include <atomic>
#include <string>
#include <thread>
#include <vector>
#include <cwctype>

constexpr UINT GestureMessage = WM_APP + 1;
constexpr UINT ReaderMessage = WM_APP + 2;
constexpr int EnableId = 10;
constexpr int VolumeUpBlockId = 11;
constexpr int VolumeDownBlockId = 12;
constexpr UINT VolumeHookMessage = WM_APP + 3;
HWND blockUpBox, blockDownBox, hookStatus;
HHOOK volumeHook;
bool blockVolume[2]{};
bool blockedDown[2]{};
bool volumeDown[2]{};
unsigned observedVolume = 0, suppressedVolume = 0;

LRESULT CALLBACK volumeKeyboard(int code, WPARAM message, LPARAM data) {
    if (code == HC_ACTION) {
        const auto& event = *reinterpret_cast<const KBDLLHOOKSTRUCT*>(data);
        const int index = event.vkCode == VK_VOLUME_UP ? 0 :
            event.vkCode == VK_VOLUME_DOWN ? 1 : -1;
        if (index >= 0 && !(event.flags & LLKHF_INJECTED)) {
            const bool up = message == WM_KEYUP || message == WM_SYSKEYUP;
            if (!up && !volumeDown[index]) {
                volumeDown[index] = true;
                blockedDown[index] = blockVolume[index];
            }
            const bool blocked = blockedDown[index];
            if (up) {
                volumeDown[index] = false;
                blockedDown[index] = false;
            }
            if (!up) {
                ++observedVolume;
                if (blocked) ++suppressedVolume;
                PostMessageW(GetParent(hookStatus),VolumeHookMessage,index,blocked);
            }
            if (blocked) return 1;
        }
    }
    return CallNextHookEx(nullptr,code,message,data);
}
HANDLE deviceHelper = nullptr;
HANDLE deviceRelease = nullptr;
HANDLE devicePause = nullptr;
std::atomic<unsigned> physicalMask{0};
constexpr UINT SwitchStatus = WM_APP + 5;
std::wstring configuredDevice;
int runDevice(bool disable) {
    if (configuredDevice.empty()) return 1;
    wchar_t system[MAX_PATH]{};
    GetSystemDirectoryW(system,MAX_PATH);
    std::wstring exe = std::wstring(system)+L"\\pnputil.exe";
    std::wstring cmd = L"\""+exe+L"\" " + (disable ? L"/disable-device " : L"/enable-device ") +
        L"\"" + configuredDevice + L"\"";
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(),cmd.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)) return 1;
    WaitForSingleObject(pi.hProcess,INFINITE);
    DWORD code=1;
    GetExitCodeProcess(pi.hProcess,&code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return static_cast<int>(code);
}
void toggleDevice(HWND window) {
    if (deviceRelease) { SetEvent(deviceRelease); return; }
    const auto name=L"Local\\SurfaceVolumeRelease-"+std::to_wstring(GetCurrentProcessId());
    deviceRelease=CreateEventW(nullptr,TRUE,FALSE,name.c_str());
    devicePause=CreateEventW(nullptr,TRUE,FALSE,(name+L"-pause").c_str());
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr,exe,MAX_PATH);
    const auto args=L"--device-helper "+std::to_wstring(GetCurrentProcessId())+L" "+std::to_wstring(reinterpret_cast<UINT_PTR>(window))+L" "+configuredDevice;
    SHELLEXECUTEINFOW launch{sizeof(launch)};
    launch.fMask=SEE_MASK_NOCLOSEPROCESS;
    launch.hwnd=window;
    launch.lpVerb=L"runas";
    launch.lpFile=exe;
    launch.lpParameters=args.c_str();
    launch.nShow=SW_HIDE;
    if (deviceRelease && ShellExecuteExW(&launch)) deviceHelper=launch.hProcess;
    else { if (deviceRelease) CloseHandle(deviceRelease); deviceRelease=nullptr; }
}
constexpr int FirstChoice = 100;
constexpr wchar_t SettingsKey[] = L"Software\\SurfaceButtonBindings";
constexpr UINT_PTR ClickTimer = 2;
constexpr ULONGLONG ClickWindow = 350;
constexpr unsigned ComboStart = 12;
constexpr std::array gestures{
    L"电源 单击", L"电源 双击", L"电源 三击", L"电源 长按",
    L"音量 + 单击", L"音量 + 双击", L"音量 + 三击", L"音量 + 长按",
    L"音量 - 单击", L"音量 - 双击", L"音量 - 三击", L"音量 - 长按",
    L"电源 + 音量 +", L"电源 + 音量 -", L"音量 + 和 音量 -"
};
struct Action { const wchar_t* label; const wchar_t* target; };
constexpr std::array actions{
    Action{L"无操作", nullptr},
    Action{L"打开记事本", L"notepad.exe"},
    Action{L"打开计算器", L"calc.exe"},
    Action{L"打开文件资源管理器", L"explorer.exe"},
    Action{L"打开 Windows 设置", L"ms-settings:"},
    Action{L"打开显示设置", L"ms-settings:display"},
    Action{L"区域截图", L"ms-screenclip:"},
    Action{L"打开任务管理器", L"taskmgr.exe"},
    Action{L"打开屏幕键盘", L"osk.exe"},
    Action{L"打开放大镜", L"magnify.exe"},
    Action{L"打开声音设置", L"ms-settings:sound"},
    Action{L"打开蓝牙设置", L"ms-settings:bluetooth"},
    Action{L"打开 Wi-Fi 设置", L"ms-settings:network-wifi"},
    Action{L"打开电源与电池设置", L"ms-settings:powersleep"},
    Action{L"打开辅助功能设置", L"ms-settings:easeofaccess"}
};
constexpr std::array<int,gestures.size()> oldSlots{0,-1,-1,1,2,-1,-1,3,4,-1,-1,5,6,7,8};
std::wstring slotName(unsigned i) {
    return oldSlots[i] < 0 ? L"multi-" + std::to_wstring(i) : std::to_wstring(oldSlots[i]);
}
struct Clicks { unsigned count = 0; ULONGLONG deadline = 0; };
std::array<Clicks,3> pending{};
std::array<HWND,gestures.size()> choices{};
HWND enableBox, readerStatus, activity;
std::atomic<bool> reading{false};
std::atomic<bool> enabled{false};
std::atomic<unsigned> generation{0};

struct Button { ULONGLONG since = 0; bool inCombo = false; };
struct GestureState {
    std::array<Button,3> buttons{};
    unsigned previous = 0;
    unsigned combinations = 0;
    bool initialized = false;
    unsigned blocked = 0;
    unsigned epoch = 0;

    void report(HWND window, unsigned mask, ULONGLONG tick) {
        const auto currentEpoch = generation.load();
        if (currentEpoch != epoch) {
            initialized = false;
            epoch = currentEpoch;
        }
        if (!initialized) {
            initialized = true;
            blocked = previous & mask;
            previous = 0;
            buttons = {};
            combinations = 0;
        }
        blocked &= mask;
        mask &= ~blocked;
        const auto emit = [&](unsigned gesture) {
            PostMessageW(window, GestureMessage, gesture, epoch);
        };
        for (unsigned i=0;i<buttons.size();++i) {
            const unsigned bit = 1u << i;
            if ((mask & bit) && !(previous & bit)) buttons[i] = {tick,false};
        }
        constexpr unsigned pairs[][2] = {{0,1},{0,2},{1,2}};
        unsigned overlap = 0;
        for (unsigned i=0;i<3;++i) {
            const auto a = pairs[i][0], b = pairs[i][1];
            const unsigned bits = (1u<<a) | (1u<<b);
            if ((mask & bits) != bits) continue;
            if (tick-buttons[a].since >= 5000 || tick-buttons[b].since >= 5000) continue;
            overlap |= 1u<<i;
            if (!(combinations & (1u<<i))) {
                buttons[a].inCombo = buttons[b].inCombo = true;
                emit(ComboStart+i);
            }
        }
        combinations = overlap;
        for (unsigned i=0;i<buttons.size();++i) {
            const unsigned bit = 1u << i;
            if (!(previous & bit) || (mask & bit) || buttons[i].inCombo) continue;
            const auto duration = tick-buttons[i].since;
            if (duration < 5000) emit(i*4 + (duration >= 500 ? 3u : 0u));
        }
        previous = mask;
    }
};

std::vector<std::wstring> candidates() {
    UINT count = 0;
    if (GetRawInputDeviceList(nullptr,&count,sizeof(RAWINPUTDEVICELIST)) == UINT(-1) || !count)
        return {};
    std::vector<RAWINPUTDEVICELIST> list(count);
    const UINT found = GetRawInputDeviceList(list.data(),&count,sizeof(RAWINPUTDEVICELIST));
    std::vector<std::wstring> paths;
    if (found == UINT(-1)) return paths;
    for (UINT i=0;i<found;++i) {
        RID_DEVICE_INFO info{};
        info.cbSize = sizeof(info);
        UINT bytes = sizeof(info);
        const HANDLE device = list[i].hDevice;
        if (list[i].dwType != RIM_TYPEHID ||
            GetRawInputDeviceInfoW(device,RIDI_DEVICEINFO,&info,&bytes) == UINT(-1)) continue;
        const auto& hid = info.hid;
        if (hid.usUsagePage != 1 || hid.usUsage != 0x0d) continue;
        UINT chars = 0;
        GetRawInputDeviceInfoW(device,RIDI_DEVICENAME,nullptr,&chars);
        if (!chars) continue;
        std::wstring path(chars+1,L'\0');
        if (GetRawInputDeviceInfoW(device,RIDI_DEVICENAME,path.data(),&chars) != UINT(-1))
            paths.emplace_back(path.c_str());
    }
    return paths;
}

void readButtons(HWND window) {
    DWORD error = ERROR_NOT_FOUND;
    for (const auto& path : candidates()) {
        HANDLE file = CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,
            nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            error = GetLastError();
            continue;
        }
        PHIDP_PREPARSED_DATA pp = nullptr;
        HIDP_CAPS caps{};
        if (!HidD_GetPreparsedData(file,&pp) || HidP_GetCaps(pp,&caps) != HIDP_STATUS_SUCCESS ||
            caps.InputReportByteLength == 0) {
            if (pp) HidD_FreePreparsedData(pp);
            CloseHandle(file);
            error = ERROR_INVALID_DATA;
            continue;
        }
        const ULONG maximum = HidP_MaxUsageListLength(HidP_Input,0,pp);
        if (!maximum) {
            HidD_FreePreparsedData(pp);
            CloseHandle(file);
            error = ERROR_INVALID_DATA;
            continue;
        }
        std::vector<USAGE_AND_PAGE> active(maximum);
        GestureState state;
        state.initialized = true;
        state.epoch = generation.load();
        PostMessageW(window,ReaderMessage,TRUE,0);
        std::vector<char> report(caps.InputReportByteLength);
        DWORD bytes = 0;
        while (ReadFile(file,report.data(),static_cast<DWORD>(report.size()),&bytes,nullptr) && bytes) {
            if (bytes != report.size()) continue;
            ULONG count = maximum;
            if (HidP_GetUsagesEx(HidP_Input,0,active.data(),&count,pp,report.data(),bytes) != HIDP_STATUS_SUCCESS)
                continue;
            unsigned mask = 0;
            for (ULONG i=0;i<count;++i) {
                const auto& usage = active[i];
                if (usage.UsagePage == 1 && usage.Usage == 0x81) mask |= 1;
                if (usage.UsagePage == 0x0c && usage.Usage == 0xe9) mask |= 2;
                if (usage.UsagePage == 0x0c && usage.Usage == 0xea) mask |= 4;
            }
            physicalMask = mask;
            state.report(window,mask,GetTickCount64());
        }
        error = GetLastError();
        HidD_FreePreparsedData(pp);
        CloseHandle(file);
        break;
    }
    enabled = false;
    ++generation;
    PostMessageW(window,ReaderMessage,FALSE,error);
    reading = false;
}

HWND control(HWND parent,const wchar_t* type,const wchar_t* text,DWORD style,
             int x,int y,int w,int h,int id=0) {
    const UINT dpi = GetDpiForWindow(parent);
    HWND child = CreateWindowExW(0,type,text,WS_CHILD|WS_VISIBLE|style,
        MulDiv(x,dpi,96),MulDiv(y,dpi,96),MulDiv(w,dpi,96),MulDiv(h,dpi,96),
        parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
    SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);
    return child;
}

void startReader(HWND window) {
    if (!reading.exchange(true)) std::thread(readButtons,window).detach();
}

void runGesture(HWND window,unsigned index) {
    const auto selection = static_cast<size_t>(SendMessageW(choices[index],CB_GETCURSEL,0,0));
    std::wstring status = gestures[index];
    if (enabled && selection < actions.size() && actions[selection].target) {
        const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(window,L"open",
            actions[selection].target,nullptr,nullptr,SW_SHOWNORMAL));
        status += result > 32 ? L"  \u2192  " : L"  \u542f\u52a8\u5931\u8d25  ";
        status += actions[selection].label;
    }
    SetWindowTextW(activity,status.c_str());
}

LRESULT CALLBACK gui(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    switch (message) {
    case WM_CREATE:
        readerStatus = control(window,L"STATIC",L"\u6b63\u5728\u8fde\u63a5 Surface HID...",0,20,18,470,24);
        enableBox = control(window,L"BUTTON",L"\u542f\u7528\u7ed1\u5b9a\uff08Windows \u539f\u6309\u952e\u52a8\u4f5c\u4ecd\u4f1a\u6267\u884c\uff09",
            BS_AUTOCHECKBOX|WS_TABSTOP|WS_DISABLED,20,48,470,26,EnableId);
        for (unsigned i=0;i<gestures.size();++i) {
            const int y = 116+static_cast<int>(i)*28;
            control(window,L"STATIC",gestures[i],0,20,y+4,175,24);
            choices[i] = control(window,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,
                200,y,290,220,FirstChoice+i);
            for (const auto& action : actions)
                SendMessageW(choices[i],CB_ADDSTRING,0,reinterpret_cast<LPARAM>(action.label));
            DWORD saved = 0, size = sizeof(saved);
            auto name = slotName(i);
            RegGetValueW(HKEY_CURRENT_USER,SettingsKey,name.c_str(),RRF_RT_REG_DWORD,nullptr,&saved,&size);
            SendMessageW(choices[i],CB_SETCURSEL,saved < actions.size() ? saved : 0,0);
        }
        blockUpBox = control(window,L"BUTTON",L"启用电源键自动暂停机身音量",BS_AUTOCHECKBOX|WS_TABSTOP,
            20,79,231,26,VolumeUpBlockId);
        blockDownBox = control(window,L"BUTTON",L"设备模式（管理员授权）",BS_AUTOCHECKBOX|WS_TABSTOP,
            253,79,231,26,VolumeDownBlockId);
        activity = control(window,L"STATIC",L"\u7b49\u5f85\u6309\u952e",0,20,544,470,24);
        hookStatus = control(window,L"STATIC",L"音量钩子：等待音量键（适用于所有键盘）",
            0,20,573,470,38);
        EnableWindow(blockDownBox,FALSE);
        if (!volumeHook) {
            const auto status = L"就绪 · 机身设备模式 " + std::to_wstring(GetLastError());
            SetWindowTextW(hookStatus,status.c_str());

            EnableWindow(blockDownBox,FALSE);
        }
        SetTimer(window,1,2000,nullptr);
        SetTimer(window,ClickTimer,25,nullptr);
        startReader(window);
        return 0;
    case SwitchStatus: {
        const auto text = std::wstring(wp == 0 ? L"机身音量已恢复 · " : wp == 1 ? L"机身音量已暂停 · " : L"设备切换失败 · ") + std::to_wstring(lp) + L" ms";
        SetWindowTextW(hookStatus,text.c_str());
        return 0;
    }
    case WM_TIMER:
        if (devicePause) {
            static bool active = false;
            const auto mask = physicalMask.load();
            if (mask & 1) active = true;
            if (!mask) active = false;
            if (active) SetEvent(devicePause); else ResetEvent(devicePause);
        }
        if (deviceHelper && WaitForSingleObject(deviceHelper,0)==WAIT_OBJECT_0) {
            DWORD code=1;
            GetExitCodeProcess(deviceHelper,&code);
            CloseHandle(deviceHelper); deviceHelper=nullptr;
            CloseHandle(deviceRelease); deviceRelease=nullptr;
            if (devicePause) CloseHandle(devicePause); devicePause=nullptr;
            SendMessageW(blockUpBox,BM_SETCHECK,BST_UNCHECKED,0);
            SetWindowTextW(hookStatus,code==0 ? L"机身音量已恢复" : L"操作失败，请检查设备状态");
        }
        if (wp == ClickTimer) {
            const auto now = GetTickCount64();
            for (unsigned i=0;i<pending.size();++i) {
                auto& clicks = pending[i];
                if (clicks.count && now >= clicks.deadline) {
                    const auto index = i*4+clicks.count-1;
                    clicks = {};
                    runGesture(window,index);
                }
            }
        } else startReader(window);
        return 0;
    case ReaderMessage:
        if (wp) {
            SetWindowTextW(readerStatus,L"Surface HID \u5df2\u8fde\u63a5");
            EnableWindow(enableBox,TRUE);
        } else {
            pending = {};
            SendMessageW(enableBox,BM_SETCHECK,BST_UNCHECKED,0);
            EnableWindow(enableBox,FALSE);
            auto status = L"\u7b49\u5f85 Surface HID \u00b7 Win32 " + std::to_wstring(lp);
            SetWindowTextW(readerStatus,status.c_str());
        }
        return 0;
    case VolumeHookMessage: {
        auto status = std::wstring(wp == 0 ? L"音量 + " : L"音量 - ") +
            (lp ? L"已拦截" : L"已放行") + L" · 收到 " + std::to_wstring(observedVolume) +
            L" / 拦截 " + std::to_wstring(suppressedVolume);
        SetWindowTextW(hookStatus,status.c_str());
        return 0;
    }
    case WM_COMMAND:
        if (HIWORD(wp) == BN_CLICKED &&
            (LOWORD(wp) == VolumeUpBlockId || LOWORD(wp) == VolumeDownBlockId)) {
            toggleDevice(window);
            const unsigned index = LOWORD(wp) == VolumeUpBlockId ? 0u : 1u;
            blockVolume[index] = SendMessageW(index == 0 ? blockUpBox : blockDownBox,
                BM_GETCHECK,0,0) == BST_CHECKED;
        } else if (LOWORD(wp) == EnableId && HIWORD(wp) == BN_CLICKED) {
            pending = {};
            ++generation;
            enabled = SendMessageW(enableBox,BM_GETCHECK,0,0) == BST_CHECKED;
        } else if (HIWORD(wp) == CBN_SELCHANGE && LOWORD(wp) >= FirstChoice &&
                   LOWORD(wp) < FirstChoice+choices.size()) {
            const unsigned i = LOWORD(wp)-FirstChoice;
            DWORD selected = static_cast<DWORD>(SendMessageW(choices[i],CB_GETCURSEL,0,0));
            if (selected < actions.size() && RegSetKeyValueW(HKEY_CURRENT_USER,SettingsKey,
                slotName(i).c_str(),REG_DWORD,&selected,sizeof(selected)) != ERROR_SUCCESS)
                SetWindowTextW(activity,L"\u65e0\u6cd5\u4fdd\u5b58\u7ed1\u5b9a");
        }
        return 0;
    case GestureMessage:
        if (wp >= gestures.size() || static_cast<unsigned>(lp) != generation) return 0;
        if (wp >= ComboStart) {
            constexpr unsigned pairs[][2] = {{0,1},{0,2},{1,2}};
            for (auto member : pairs[wp-ComboStart]) pending[member] = {};
            runGesture(window,static_cast<unsigned>(wp));
        } else if (wp % 4 == 3) {
            pending[wp/4] = {};
            runGesture(window,static_cast<unsigned>(wp));
        } else {
            auto& clicks = pending[wp/4];
            const auto now = GetTickCount64();
            if (clicks.count && now >= clicks.deadline) {
                runGesture(window,static_cast<unsigned>(wp)+clicks.count-1);
                clicks = {};
            }
            if (++clicks.count == 3) {
                runGesture(window,static_cast<unsigned>(wp)+2);
                clicks = {};
            } else {
                clicks.deadline = now+ClickWindow;
            }
        }
        return 0;
    case WM_DESTROY:
        if (deviceRelease) SetEvent(deviceRelease);
        if (volumeHook) UnhookWindowsHookEx(volumeHook);
        enabled = false;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window,message,wp,lp);
}

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR args,int show) {
    if (wcsncmp(args,L"--device-helper ",16)==0) {
        DWORD pid=wcstoul(args+16,nullptr,10);
        HANDLE parent=OpenProcess(SYNCHRONIZE,FALSE,pid);
        const auto name=L"Local\\SurfaceVolumeRelease-"+std::to_wstring(pid);
        HANDLE release=OpenEventW(SYNCHRONIZE,FALSE,name.c_str());
        if (!parent || !release) return 1;
        HANDLE pause=OpenEventW(SYNCHRONIZE,FALSE,(name+L"-pause").c_str());
        const wchar_t* separator=wcschr(args+16,L' ');
        HWND window=separator ? reinterpret_cast<HWND>(static_cast<UINT_PTR>(_wcstoui64(separator+1,nullptr,10))) : nullptr;
        if (!pause) return 1;
        const wchar_t* target=separator ? wcschr(separator+1,L' ') : nullptr;
        if (!target) return 1;
        configuredDevice=target+1;
        bool disabled=false, timedOut=false;
        ULONGLONG since=0;
        HANDLE stop[]{parent,release};
        while (WaitForMultipleObjects(2,stop,FALSE,10)==WAIT_TIMEOUT) {
            const bool requested=WaitForSingleObject(pause,0)==WAIT_OBJECT_0;
            if (!requested) timedOut=false;
            const bool wanted=requested && !timedOut;
            if (disabled && GetTickCount64()-since>=10000) timedOut=true;
            if (disabled != (wanted && !timedOut)) {
                const bool target=wanted && !timedOut;
                const auto start=GetTickCount64();
                const int code=runDevice(target);
                PostMessageW(window,SwitchStatus,code==0 ? (target ? 1 : 0) : 2,GetTickCount64()-start);
                if (code!=0) { runDevice(false); CloseHandle(pause); return 1; }
                disabled=target;
                since=GetTickCount64();
            }
        }
        const int restored=disabled ? runDevice(false) : 0;
        CloseHandle(pause); CloseHandle(parent); CloseHandle(release);
        return restored;
    }
    wchar_t saved[1024]{};
    DWORD savedSize=sizeof(saved);
    if (RegGetValueW(HKEY_CURRENT_USER,SettingsKey,L"VolumeDevice",RRF_RT_REG_SZ,nullptr,saved,&savedSize)==ERROR_SUCCESS) configuredDevice=saved;
    HANDLE singleton = CreateMutexW(nullptr,FALSE,L"Local\\SurfaceButtonBindings");
    if (!singleton || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (singleton) CloseHandle(singleton);
        MessageBoxW(nullptr,L"\u5e94\u7528\u5df2\u8fd0\u884c\u6216\u65e0\u6cd5\u542f\u52a8\u3002",
            L"Surface Button Bindings",MB_OK|MB_ICONINFORMATION);
        return 1;
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
    WNDCLASSW wc{};
    wc.hInstance = instance;
    wc.lpfnWndProc = gui;
    wc.lpszClassName = L"SurfaceButtonBindingsWindow";
    wc.hCursor = LoadCursorW(nullptr,IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    if (!RegisterClassW(&wc)) return 1;
    const DWORD style = WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX;
    const UINT dpi = GetDpiForSystem();
    RECT bounds{0,0,MulDiv(512,dpi,96),MulDiv(615,dpi,96)};
    AdjustWindowRectExForDpi(&bounds,style,FALSE,0,dpi);
    HWND window = CreateWindowExW(0,wc.lpszClassName,L"Surface Button Bindings",style,
        CW_USEDEFAULT,CW_USEDEFAULT,bounds.right-bounds.left,bounds.bottom-bounds.top,
        nullptr,nullptr,instance,nullptr);
    if (!window) return 1;
    ShowWindow(window,show);
    MSG msg{};
    BOOL result;
    while ((result=GetMessageW(&msg,nullptr,0,0)) > 0)
        if (!IsDialogMessageW(window,&msg)) DispatchMessageW(&msg);
    ExitProcess(result == -1 ? 1 : 0);
}







