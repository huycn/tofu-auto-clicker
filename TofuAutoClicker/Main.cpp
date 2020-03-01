#include <windows.h>
#include <commctrl.h>
#include "resource.h"

#include <array>
#include <vector>
#include <string>

#pragma comment(linker, \
  "\"/manifestdependency:type='Win32' "\
  "name='Microsoft.Windows.Common-Controls' "\
  "version='6.0.0.0' "\
  "processorArchitecture='*' "\
  "publicKeyToken='6595b64144ccf1df' "\
  "language='*'\"")

#pragma comment(lib, "ComCtl32.lib")

static const std::wstring RegistryPath = L"Software\\Tofu Realm\\Tofu Auto-Clicker";


INT_PTR CALLBACK DialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
bool ReadRegistry(HKEY hKey, const wchar_t* szSubKey, std::vector<std::pair<const wchar_t*, DWORD>>& values);
bool WriteRegistry(HKEY hKey, const wchar_t* szSubKey, const std::vector<std::pair<const wchar_t*, DWORD>>& values);


int WINAPI wWinMain(_In_ HINSTANCE hInst, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
    InitCommonControls();
    HWND hDlg = CreateDialogParam(hInst, MAKEINTRESOURCE(IDD_MAIN_DIALOG), 0, DialogProc, 0);

    // Icon in the title
    HICON hicon = (HICON)LoadImage(hInst, MAKEINTRESOURCE(IDI_ICON1), IMAGE_ICON, 0, 0, LR_DEFAULTCOLOR | LR_DEFAULTSIZE);
    SendMessage(hDlg, WM_SETICON, ICON_BIG, (LPARAM)hicon);

    ShowWindow(hDlg, nCmdShow);

    MSG msg;
    BOOL bRet;
    while ((bRet = GetMessage(&msg, 0, 0, 0)) != 0) {
        if (bRet == -1) {
            return -1;
        }

        if (!IsDialogMessage(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return 0;
}

struct CommonConfig
{
    bool AvoidConcurrentTimer = true;

    void ReadDialogItemsValues(HWND hDlg) {
        AvoidConcurrentTimer = SendDlgItemMessage(hDlg, IDC_CHECK_SINGLE_REPEAT, BM_GETCHECK, 0, 0) == BST_CHECKED;
    }

    void Load(HWND hDlg) {
        std::vector<std::pair<const wchar_t*, DWORD>> regValues = { {L"AvoidConcurrent", AvoidConcurrentTimer ? 1 : 0 } };
        if (ReadRegistry(HKEY_CURRENT_USER, RegistryPath.c_str(), regValues)) {
            AvoidConcurrentTimer = regValues[0].second != 0;
        }
        SendDlgItemMessage(hDlg, IDC_CHECK_SINGLE_REPEAT, BM_SETCHECK, AvoidConcurrentTimer ? BST_CHECKED : BST_UNCHECKED, 0);
    }

    void Save(HWND hDlg) {
        ReadDialogItemsValues(hDlg);
        std::vector<std::pair<const wchar_t*, DWORD>> regValues = { {L"AvoidConcurrent", AvoidConcurrentTimer ? 1 : 0 } };
        WriteRegistry(HKEY_CURRENT_USER, RegistryPath.c_str(), regValues);
    }
};

struct HotKeyConfig
{
    int VirtualKeyCode = 0;
    int Modifiers = 0;

    WPARAM toWParam() {
        return MAKEWORD(VirtualKeyCode, Modifiers);
    }

    BOOL ConfigureHotKey(HWND hDlg, int id) {
        if (VirtualKeyCode == 0) {
            return FALSE;
        }

        UINT m = 0;
        if (Modifiers & HOTKEYF_ALT) {
            m |= MOD_ALT;
        }
        if (Modifiers & HOTKEYF_CONTROL) {
            m |= MOD_CONTROL;
        }
        if (Modifiers & HOTKEYF_SHIFT) {
            m |= MOD_SHIFT;
        }

        return RegisterHotKey(hDlg, id, m, VirtualKeyCode);
    }

    static HotKeyConfig FromResult(LRESULT r) {
        return HotKeyConfig{
            LOBYTE(r),
            HIBYTE(r),
        };
    }
};

class ButtonConfig
{
public:
    const int ConfigId;
    const wchar_t* const ButtonName;
    const int MouseDownEvent;
    const int IdCheckEnabled;
    const int IdHotkeyTrigger;
    const int IdCheckRepeat;
    const int IdEditRepeatDelay;

    ButtonConfig(int id, const wchar_t* name, int downEvent, int idCtlEnabled, int idCtlHotkey, int idCtlRepeat, int idCtlDelay):
        ConfigId(id),
        ButtonName(name),
        MouseDownEvent(downEvent),
        IdCheckEnabled(idCtlEnabled),
        IdHotkeyTrigger(idCtlHotkey),
        IdCheckRepeat(idCtlRepeat),
        IdEditRepeatDelay(idCtlDelay) {}

    void Load(HWND hDlg) {

        auto regValues = GetRegistryValues();
        if (ReadRegistry(HKEY_CURRENT_USER, (RegistryPath + L"\\" + std::wstring(ButtonName)).c_str(), regValues)) {
            SetFromRegistryValues(regValues);
        }

        SendDlgItemMessage(hDlg, IdCheckEnabled, BM_SETCHECK, isEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
        SendDlgItemMessage(hDlg, IdHotkeyTrigger, HKM_SETHOTKEY, hotKey.toWParam(), 0);
        SendDlgItemMessage(hDlg, IdCheckRepeat, BM_SETCHECK, isRepeated ? BST_CHECKED : BST_UNCHECKED, 0);
        SetDlgItemInt(hDlg, IdEditRepeatDelay, repeatDelay, FALSE);
    }

    void Save(HWND hDlg) {
        GetDialogItemsValues(hDlg);
        WriteRegistry(HKEY_CURRENT_USER, (RegistryPath + L"\\" + std::wstring(ButtonName)).c_str(), GetRegistryValues());
    }

    void Start(HWND hDlg) {
        GetDialogItemsValues(hDlg);

        if (!isEnabled) {
            return;
        }
        hotKey.ConfigureHotKey(hDlg, ConfigId);
    }

    void Stop(HWND hDlg) {
        if (timer != NULL) {
            KillTimer(hDlg, timer);
            timer = NULL;
        }
        UnregisterHotKey(hDlg, ConfigId);
    }

    void OnHotKeyReceived(HWND hDlg) {
        if (!isRepeated || repeatDelay <= 0) {
            SendMouseClick();
            return;
        }

        if (!isActive)
            Activate(hDlg);
        else
            Deactivate(hDlg);
    }

    void Activate(HWND hDlg) {
        if (!isActive) {
            isActive = true;
            SendMouseClick();
            timer = SetTimer(hDlg, ConfigId, repeatDelay, (TIMERPROC)NULL);
        }
    }

    void Deactivate(HWND hDlg) {
        if (isActive) {
            isActive = false;
            if (timer != NULL) {
                KillTimer(hDlg, timer);
                timer = NULL;
            }
        }
    }

    void OnTimeout(HWND hDlg) {
        SendMouseClick();
    }

private:
    void GetDialogItemsValues(HWND hDlg) {
        isEnabled = SendDlgItemMessage(hDlg, IdCheckEnabled, BM_GETCHECK, 0, 0) == BST_CHECKED;
        hotKey = HotKeyConfig::FromResult(SendDlgItemMessage(hDlg, IdHotkeyTrigger, HKM_GETHOTKEY, 0, 0));
        isRepeated = SendDlgItemMessage(hDlg, IdCheckRepeat, BM_GETCHECK, 0, 0) == BST_CHECKED;
        repeatDelay = GetDlgItemInt(hDlg, IdEditRepeatDelay, nullptr, FALSE);
    }

    void SendMouseClick() {
        INPUT input;
        memset(&input, 0, sizeof(input));

        input.type = INPUT_MOUSE;
        input.mi.dwFlags = MouseDownEvent;
        SendInput(1, &input, sizeof(INPUT));

        input.mi.dwFlags = (MouseDownEvent << 1); // up event
        SendInput(1, &input, sizeof(INPUT));
    }

    std::vector<std::pair<const wchar_t*, DWORD>> GetRegistryValues() {
        return {
            { L"Enabled", isEnabled ? 1 : 0 },
            { L"HotkeyCode", hotKey.VirtualKeyCode },
            { L"HotkeyMod", hotKey.Modifiers },
            { L"Repeat", isRepeated ? 1 : 0 },
            { L"Delay", repeatDelay },
        };
    }

    void SetFromRegistryValues(const std::vector<std::pair<const wchar_t*, DWORD>>& values) {
        for (auto& val : values) {
            if (wcscmp(val.first, L"Enabled") == 0) {
                isEnabled = val.second != 0;
            }
            else if (wcscmp(val.first, L"HotkeyCode") == 0) {
                hotKey.VirtualKeyCode = val.second;
            }
            else if (wcscmp(val.first, L"HotkeyMod") == 0) {
                hotKey.Modifiers = val.second;
            }
            else if (wcscmp(val.first, L"Repeat") == 0) {
                isRepeated = val.second != 0;
            }
            else if (wcscmp(val.first, L"Delay") == 0) {
                repeatDelay = val.second;
            }
        }
    }

    bool isEnabled = false;
    bool isActive = false;
    HotKeyConfig hotKey;
    bool isRepeated = true;
    UINT repeatDelay = 500;
    UINT_PTR timer = NULL;
};

static std::array<ButtonConfig, 3> configs = {
    ButtonConfig(1, L"ButtonLeft", MOUSEEVENTF_LEFTDOWN, IDC_CHECK_ENABLE_LEFT, IDC_HOTKEY_TRIGGER_LEFT, IDC_CHECK_REPEAT_LEFT, IDC_EDIT_DELAY_LEFT),
    ButtonConfig(2, L"ButtonRight", MOUSEEVENTF_RIGHTDOWN, IDC_CHECK_ENABLE_RIGHT, IDC_HOTKEY_TRIGGER_RIGHT, IDC_CHECK_REPEAT_RIGHT, IDC_EDIT_DELAY_RIGHT),
    ButtonConfig(3, L"ButtonMiddle", MOUSEEVENTF_MIDDLEDOWN, IDC_CHECK_ENABLE_MIDDLE, IDC_HOTKEY_TRIGGER_MIDDLE, IDC_CHECK_REPEAT_MIDDLE, IDC_EDIT_DELAY_MIDDLE),
};

CommonConfig commonConfig;

INT_PTR CALLBACK DialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
        commonConfig.Load(hDlg);
        for (auto& config : configs) {
            config.Load(hDlg);
        }
        break;

    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE) {
            commonConfig.ReadDialogItemsValues(hDlg);
            for (auto& config : configs) {
                config.Start(hDlg);
            }
        }
        else {
            for (auto& config : configs) {
                config.Stop(hDlg);
            }
        }
        break;

    case WM_HOTKEY:
        if (wParam > 0 && wParam <= configs.size()) {
            if (commonConfig.AvoidConcurrentTimer) {
                for (size_t i = 0; i < configs.size(); ++i) {
                    if (i + 1 != wParam) {
                        configs[i].Deactivate(hDlg);
                    }
                }
            }
            configs[(int)wParam - 1].OnHotKeyReceived(hDlg);
        }
        break;

    case WM_TIMER:
        if (wParam > 0 && wParam <= configs.size()) {
            configs[(int)wParam - 1].OnTimeout(hDlg);
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDCANCEL:
            SendMessage(hDlg, WM_CLOSE, 0, 0);
            return TRUE;
        }
        break;

    case WM_CLOSE:
        commonConfig.Save(hDlg);
        for (auto& config : configs) {
            config.Save(hDlg);
        }
        DestroyWindow(hDlg);
        return TRUE;

    case WM_DESTROY:
        PostQuitMessage(0);
        return TRUE;
    }

    return FALSE;
}

bool ReadRegistry(HKEY hKey, const wchar_t* szSubKey, std::vector<std::pair<const wchar_t*, DWORD>>& values)
{
    HKEY hkSubKey;
    if (RegOpenKeyEx(hKey, szSubKey, 0, KEY_READ, &hkSubKey) == ERROR_SUCCESS)
    {
        for (auto& value : values) {
            DWORD dwType;
            DWORD cbData = sizeof(DWORD);
            RegGetValue(hKey, szSubKey, value.first, RRF_RT_REG_DWORD, &dwType, &value.second, &cbData);
        }
        RegCloseKey(hkSubKey);
        return true;
    }
    return false;
}

bool WriteRegistry(HKEY hKey, const wchar_t* szSubKey, const std::vector<std::pair<const wchar_t*, DWORD>>& values)
{
    HKEY hkSubKey;
    if (RegCreateKeyEx(hKey, szSubKey, 0, NULL, 0, KEY_WRITE, NULL, &hkSubKey, NULL) == ERROR_SUCCESS)
    {
        for (auto& value : values) {
            RegSetValueEx(hkSubKey, value.first, 0, REG_DWORD, (const BYTE*)&value.second, sizeof(DWORD));
        }
        RegCloseKey(hkSubKey);
        return true;
    }
    return false;
}
