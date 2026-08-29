#include <windows.h>
#include <shellapi.h>
#include "resource.h"

#include <algorithm>
#include <cwctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

constexpr UINT WM_SHOW_SNIPPETS = WM_APP + 1;
constexpr UINT WM_TRAY = WM_APP + 2;
constexpr UINT ID_HOTKEY = 9100;
constexpr UINT ID_TRAY_EXIT = 9001;
constexpr UINT ID_TRAY_RELOAD = 9002;
constexpr UINT ID_RELOAD = 9003;
constexpr UINT ID_EXIT = 9004;
constexpr UINT ID_FIRST_SNIPPET = 10000;
constexpr UINT DEFAULT_HOTKEY_MODIFIERS = MOD_CONTROL | MOD_SHIFT;
constexpr UINT DEFAULT_HOTKEY_VK = VK_SPACE;

struct MenuNode {
    std::wstring name;
    std::vector<std::unique_ptr<MenuNode>> children;
    std::wstring text;
    bool isSnippet = false;
};

HINSTANCE g_instance = nullptr;
HWND g_window = nullptr;
POINT g_clickPoint{};
HWND g_targetWindow = nullptr;
MenuNode g_root{L"Snip"};
std::unordered_map<UINT, std::wstring> g_commands;
UINT g_nextCommand = ID_FIRST_SNIPPET;
NOTIFYICONDATAW g_tray{};
std::filesystem::path g_configPath;
UINT g_hotkeyModifiers = DEFAULT_HOTKEY_MODIFIERS;
UINT g_hotkeyVk = DEFAULT_HOTKEY_VK;
bool g_hotkeyRegistered = false;
HWND g_previewWindow = nullptr;
std::wstring g_previewText;

std::wstring Upper(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towupper(ch)); });
    return value;
}

std::wstring Trim(std::wstring value) {
    const auto first = value.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return L"";
    const auto last = value.find_last_not_of(L" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::wstring Utf8ToWide(const std::string& input) {
    if (input.empty()) return L"";
    const int length = MultiByteToWideChar(CP_UTF8, 0, input.data(),
                                           static_cast<int>(input.size()), nullptr, 0);
    std::wstring result(length, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, input.data(), static_cast<int>(input.size()),
                        result.data(), length);
    return result;
}

std::wstring DecodeText(std::wstring text) {
    std::wstring result;
    result.reserve(text.size());
    bool escaped = false;
    for (wchar_t ch : text) {
        if (escaped) {
            result += (ch == L'n') ? L'\n' : (ch == L't' ? L'\t' : ch);
            escaped = false;
        } else if (ch == L'\\') {
            escaped = true;
        } else {
            result += ch;
        }
    }
    if (escaped) result += L'\\';
    return result;
}

std::vector<std::wstring> SplitPath(const std::wstring& path) {
    std::vector<std::wstring> parts;
    std::wistringstream stream(path);
    std::wstring part;
    while (std::getline(stream, part, L'/')) {
        part = Trim(part);
        if (!part.empty()) parts.push_back(part);
    }
    return parts;
}

bool ParseHotkey(const std::wstring& value, UINT& modifiers, UINT& virtualKey) {
    modifiers = 0;
    virtualKey = 0;
    std::wistringstream stream(value);
    std::wstring token;
    while (std::getline(stream, token, L'+')) {
        token = Upper(Trim(token));
        if (token == L"CTRL" || token == L"CONTROL") modifiers |= MOD_CONTROL;
        else if (token == L"SHIFT") modifiers |= MOD_SHIFT;
        else if (token == L"ALT") modifiers |= MOD_ALT;
        else if (token == L"WIN" || token == L"WINDOWS") modifiers |= MOD_WIN;
        else if (token == L"SPACE") virtualKey = VK_SPACE;
        else if (token == L"TAB") virtualKey = VK_TAB;
        else if (token == L"ENTER" || token == L"RETURN") virtualKey = VK_RETURN;
        else if (token == L"ESC" || token == L"ESCAPE") virtualKey = VK_ESCAPE;
        else if (token == L"UP") virtualKey = VK_UP;
        else if (token == L"DOWN") virtualKey = VK_DOWN;
        else if (token == L"LEFT") virtualKey = VK_LEFT;
        else if (token == L"RIGHT") virtualKey = VK_RIGHT;
        else if (token == L"F1") virtualKey = VK_F1;
        else if (token == L"F2") virtualKey = VK_F2;
        else if (token == L"F3") virtualKey = VK_F3;
        else if (token == L"F4") virtualKey = VK_F4;
        else if (token == L"F5") virtualKey = VK_F5;
        else if (token == L"F6") virtualKey = VK_F6;
        else if (token == L"F7") virtualKey = VK_F7;
        else if (token == L"F8") virtualKey = VK_F8;
        else if (token == L"F9") virtualKey = VK_F9;
        else if (token == L"F10") virtualKey = VK_F10;
        else if (token == L"F11") virtualKey = VK_F11;
        else if (token == L"F12") virtualKey = VK_F12;
        else if (token.size() == 1) virtualKey = static_cast<UINT>(VkKeyScanW(token[0]) & 0xff);
        else return false;
    }
    return virtualKey != 0;
}

MenuNode* FindOrCreate(MenuNode& parent, const std::wstring& name) {
    for (auto& child : parent.children) {
        if (!child->isSnippet && child->name == name) return child.get();
    }
    auto node = std::make_unique<MenuNode>();
    node->name = name;
    auto* result = node.get();
    parent.children.push_back(std::move(node));
    return result;
}

bool LoadConfiguration() {
    g_root.children.clear();
    g_commands.clear();
    g_nextCommand = ID_FIRST_SNIPPET;
    g_hotkeyModifiers = DEFAULT_HOTKEY_MODIFIERS;
    g_hotkeyVk = DEFAULT_HOTKEY_VK;

    std::ifstream file(g_configPath, std::ios::binary);
    if (!file) return false;

    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const auto firstTab = line.find('\t');
        if (firstTab == std::string::npos) continue;
        const auto type = Trim(Utf8ToWide(line.substr(0, firstTab)));
        if (type == L"hotkey") {
            UINT modifiers = 0;
            UINT virtualKey = 0;
            if (ParseHotkey(Utf8ToWide(line.substr(firstTab + 1)), modifiers, virtualKey)) {
                g_hotkeyModifiers = modifiers;
                g_hotkeyVk = virtualKey;
            }
            continue;
        }
        if (type != L"item") continue;
        const auto secondTab = line.find('\t', firstTab + 1);
        if (secondTab == std::string::npos) continue;
        const auto pathText = Trim(Utf8ToWide(line.substr(firstTab + 1, secondTab - firstTab - 1)));
        const auto parts = SplitPath(pathText);
        if (parts.empty()) continue;
        MenuNode* parent = &g_root;
        for (size_t i = 0; i + 1 < parts.size(); ++i) parent = FindOrCreate(*parent, parts[i]);
        auto item = std::make_unique<MenuNode>();
        item->name = parts.back();
        item->text = DecodeText(Utf8ToWide(line.substr(secondTab + 1)));
        item->isSnippet = true;
        parent->children.push_back(std::move(item));
    }
    return true;
}

bool RegisterConfiguredHotkey() {
    if (g_hotkeyRegistered) {
        UnregisterHotKey(g_window, ID_HOTKEY);
        g_hotkeyRegistered = false;
    }
    g_hotkeyRegistered = RegisterHotKey(g_window, ID_HOTKEY, g_hotkeyModifiers, g_hotkeyVk) != FALSE;
    return g_hotkeyRegistered;
}

LRESULT CALLBACK PreviewProc(HWND window, UINT message, WPARAM, LPARAM lParam) {
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        FillRect(dc, &client, reinterpret_cast<HBRUSH>(COLOR_INFOBK + 1));
        client.left += 8;
        client.top += 6;
        client.right -= 8;
        client.bottom -= 6;
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, GetSysColor(COLOR_INFOTEXT));
        DrawTextW(dc, g_previewText.c_str(), -1, &client, DT_LEFT | DT_TOP | DT_WORDBREAK);
        EndPaint(window, &paint);
        return 0;
    }
    if (message == WM_NCHITTEST) return HTTRANSPARENT;
    return DefWindowProcW(window, message, 0, lParam);
}

void HidePreview() {
    if (g_previewWindow) ShowWindow(g_previewWindow, SW_HIDE);
}

void ShowPreview(UINT command) {
    const auto found = g_commands.find(command);
    if (found == g_commands.end() || found->second.empty()) {
        HidePreview();
        return;
    }
    g_previewText = found->second;
    if (!g_previewWindow) {
        WNDCLASSW previewClass{};
        previewClass.hInstance = g_instance;
        previewClass.lpfnWndProc = PreviewProc;
        previewClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        previewClass.lpszClassName = L"SnipPreviewWindow";
        previewClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_INFOBK + 1);
        RegisterClassW(&previewClass);
        g_previewWindow = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                                          previewClass.lpszClassName, L"", WS_POPUP,
                                          0, 0, 0, 0, nullptr, nullptr, g_instance, nullptr);
    }
    HDC dc = GetDC(g_previewWindow);
    RECT desired{0, 0, 360, 0};
    DrawTextW(dc, g_previewText.c_str(), -1, &desired, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_CALCRECT);
    ReleaseDC(g_previewWindow, dc);
    const int width = 376;
    const int height = std::max(36L, desired.bottom + 20L);
    POINT point{};
    GetCursorPos(&point);
    SetWindowPos(g_previewWindow, HWND_TOPMOST, point.x + 18, point.y + 8,
                 width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(g_previewWindow, nullptr, TRUE);
}

void AddMenuItems(HMENU menu, const MenuNode& node) {
    for (const auto& child : node.children) {
        if (child->isSnippet) {
            const UINT id = g_nextCommand++;
            AppendMenuW(menu, MF_STRING, id, child->name.c_str());
            g_commands.emplace(id, child->text);
        } else {
            HMENU submenu = CreatePopupMenu();
            AddMenuItems(submenu, *child);
            AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(submenu), child->name.c_str());
        }
    }
}

static void SendEnterKey() {
    INPUT inputs[2]{};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_RETURN;
    inputs[1] = inputs[0];
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, inputs, sizeof(INPUT));
}

static bool SetClipboardText(const std::wstring& text) {
    if (!OpenClipboard(g_window)) return false;
    EmptyClipboard();
    const SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory) {
        CloseClipboard();
        return false;
    }
    void* destination = GlobalLock(memory);
    if (!destination) {
        GlobalFree(memory);
        CloseClipboard();
        return false;
    }
    memcpy(destination, text.c_str(), bytes);
    GlobalUnlock(memory);
    if (!SetClipboardData(CF_UNICODETEXT, memory)) {
        GlobalFree(memory);
        CloseClipboard();
        return false;
    }
    CloseClipboard();
    return true;
}

static void SendPaste() {
    if (g_targetWindow && IsWindow(g_targetWindow)) {
        SetForegroundWindow(g_targetWindow);
        Sleep(40);
        INPUT inputs[4]{};
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wVk = VK_CONTROL;
        inputs[1].type = INPUT_KEYBOARD;
        inputs[1].ki.wVk = 'V';
        inputs[2].type = INPUT_KEYBOARD;
        inputs[2].ki.wVk = 'V';
        inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
        inputs[3].type = INPUT_KEYBOARD;
        inputs[3].ki.wVk = VK_CONTROL;
        inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(4, inputs, sizeof(INPUT));
    }
}

static void PasteChunk(const std::wstring& chunk) {
    if (SetClipboardText(chunk)) SendPaste();
}

void PasteText(const std::wstring& text) {
    const std::wstring enterToken = L"{ENTER}";
    const std::wstring newlineToken = L"{NEWLINE}";

    // The target was captured before the menu took focus. Do not replace it
    // here: GetForegroundWindow() would return Snip after the menu closes.
    if (text.find(enterToken) == std::wstring::npos &&
        text.find(newlineToken) == std::wstring::npos) {
        if (SetClipboardText(text)) SendPaste();
        return;
    }

    size_t pos = 0;
    while (pos < text.size()) {
        const size_t nextEnter = text.find(enterToken, pos);
        const size_t nextNewline = text.find(newlineToken, pos);
        size_t nextPos = std::wstring::npos;
        std::wstring token;

        if (nextEnter != std::wstring::npos &&
            (nextNewline == std::wstring::npos || nextEnter < nextNewline)) {
            nextPos = nextEnter;
            token = enterToken;
        } else if (nextNewline != std::wstring::npos) {
            nextPos = nextNewline;
            token = newlineToken;
        }

        if (nextPos == std::wstring::npos) {
            PasteChunk(text.substr(pos));
            break;
        }

        if (nextPos > pos) PasteChunk(text.substr(pos, nextPos - pos));
        if (token == enterToken) {
            Sleep(20);
            SendEnterKey();
        } else {
            PasteChunk(L"\r\n");
        }
        pos = nextPos + token.size();
        Sleep(20);
    }
}

void ShowSnippetMenu() {
    g_targetWindow = GetForegroundWindow();
    HMENU menu = CreatePopupMenu();
    AddMenuItems(menu, g_root);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_RELOAD, L"Reload snippets.txt");
    AppendMenuW(menu, MF_STRING, ID_EXIT, L"Exit Snip");
    SetForegroundWindow(g_window);
    const UINT result = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                         g_clickPoint.x, g_clickPoint.y, g_window, nullptr);
    if (result >= ID_FIRST_SNIPPET && result < g_nextCommand) {
        PasteText(g_commands[result]);
    } else if (result == ID_RELOAD) {
        LoadConfiguration();
        RegisterConfiguredHotkey();
    } else if (result == ID_EXIT) {
        PostQuitMessage(0);
    }
    DestroyMenu(menu);
}

void AddTrayIcon() {
    g_tray.cbSize = sizeof(g_tray);
    g_tray.hWnd = g_window;
    g_tray.uID = 1;
    g_tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_tray.uCallbackMessage = WM_TRAY;
    g_tray.hIcon = LoadIconW(g_instance, MAKEINTRESOURCEW(IDI_SNIP));
    lstrcpynW(g_tray.szTip, L"Snip - text snippets", ARRAYSIZE(g_tray.szTip));
    Shell_NotifyIconW(NIM_ADD, &g_tray);
}

void RemoveTrayIcon() { Shell_NotifyIconW(NIM_DELETE, &g_tray); }

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_HOTKEY && wParam == ID_HOTKEY) {
        GetCursorPos(&g_clickPoint);
        ShowSnippetMenu();
    } else if (message == WM_MENUSELECT) {
        const UINT flags = HIWORD(wParam);
        const UINT command = LOWORD(wParam);
        if ((flags & (MF_POPUP | MF_SEPARATOR)) == 0) ShowPreview(command);
        else HidePreview();
    } else if (message == WM_TRAY && lParam == WM_RBUTTONUP) {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, ID_TRAY_RELOAD, L"Reload snippets.txt");
        AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"Exit Snip");
        POINT point{};
        GetCursorPos(&point);
        SetForegroundWindow(window);
        const UINT result = TrackPopupMenuEx(menu, TPM_RETURNCMD, point.x, point.y, window, nullptr);
        if (result == ID_TRAY_RELOAD) {
            LoadConfiguration();
            RegisterConfiguredHotkey();
        }
        if (result == ID_TRAY_EXIT) PostQuitMessage(0);
        DestroyMenu(menu);
    } else if (message == WM_DESTROY) {
        HidePreview();
        RemoveTrayIcon();
        PostQuitMessage(0);
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    g_instance = instance;
    wchar_t modulePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, modulePath, ARRAYSIZE(modulePath));
    g_configPath = std::filesystem::path(modulePath).parent_path() / L"snippets.txt";
    LoadConfiguration();

    WNDCLASSW windowClass{};
    windowClass.hInstance = instance;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.lpszClassName = L"SnipHiddenWindow";
    RegisterClassW(&windowClass);
    g_window = CreateWindowExW(WS_EX_TOOLWINDOW, windowClass.lpszClassName, L"Snip",
                               WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    if (!g_window) return 1;
    AddTrayIcon();
    if (!RegisterConfiguredHotkey()) {
        MessageBoxW(nullptr, L"Snip could not register Ctrl+Shift+Space. It may already be in use.",
                    L"Snip", MB_ICONERROR);
        RemoveTrayIcon();
        DestroyWindow(g_window);
        return 2;
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (g_hotkeyRegistered) UnregisterHotKey(g_window, ID_HOTKEY);
    RemoveTrayIcon();
    DestroyWindow(g_window);
    return 0;
}
