// Zaga Device Lock — desktop management app.
//
// A native Win32 dashboard the user or a technician opens after install. It shows
// this device's account, hardware, and lock state, reports whether the portal is
// reachable, and offers the privileged actions (enroll, arm/disarm, enter a code,
// uninstall). Read-only status is read in-process through the tested core; anything
// that writes machine state is delegated to zaga_installer.exe with a UAC prompt,
// so the app itself runs unelevated.
//
// The UI is drawn by hand (owner-drawn cards and buttons) to get the dark look
// without pulling in a UI toolkit, keeping the client dependency-free like the rest
// of the codebase.

// This app calls the wide (…W) Win32 APIs directly; defining UNICODE makes the
// resource-id macros (IDC_ARROW, IDI_APPLICATION, …) resolve to their wide form.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <string>
#include <vector>

#include "DeviceConfig.h"
#include "LocalStore.h"
#include "LockGate.h"
#include "PortalClient.h"
#include "Resource.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

// Use the modern (v6) common controls so native dialogs and focus rects look right.
#pragma comment(linker, "/manifestdependency:\"type='win32' "               \
                        "name='Microsoft.Windows.Common-Controls' "         \
                        "version='6.0.0.0' processorArchitecture='*' "      \
                        "publicKeyToken='6595b64144ccf1df' language='*'\"")

using namespace zaga;

namespace {

const wchar_t APP_CLASS[] = L"ZagaDeviceLockApp";
const wchar_t APP_TITLE[] = L"Zaga Device Lock";
const char AGENT_VERSION[] = "0.1.0";
const wchar_t INSTALLER_NAME[] = L"zaga_installer.exe";

// ---- palette (dark) --------------------------------------------------------
const COLORREF COL_BG        = RGB(0x0B, 0x0E, 0x14);
const COLORREF COL_CARD      = RGB(0x15, 0x1B, 0x24);
const COLORREF COL_CARD_EDGE = RGB(0x24, 0x2C, 0x38);
const COLORREF COL_INSET     = RGB(0x0E, 0x13, 0x1B);
const COLORREF COL_TEXT      = RGB(0xED, 0xF0, 0xF5);
const COLORREF COL_MUTED     = RGB(0x8B, 0x95, 0xA5);
const COLORREF COL_FAINT     = RGB(0x5A, 0x64, 0x74);
const COLORREF COL_ACCENT    = RGB(0x4D, 0x9F, 0xFF);
const COLORREF COL_ACCENT_HOT= RGB(0x6C, 0xB0, 0xFF);
const COLORREF COL_BTN       = RGB(0x20, 0x28, 0x35);
const COLORREF COL_BTN_HOT   = RGB(0x2A, 0x35, 0x45);
const COLORREF COL_GREEN     = RGB(0x39, 0xD3, 0x8A);
const COLORREF COL_AMBER     = RGB(0xF0, 0xB4, 0x3B);
const COLORREF COL_RED       = RGB(0xF0, 0x5C, 0x5C);
const COLORREF COL_DANGER    = RGB(0x3A, 0x1E, 0x22);
const COLORREF COL_DANGER_HOT= RGB(0x4C, 0x24, 0x2A);
const COLORREF COL_DANGER_TXT= RGB(0xF0, 0x8A, 0x8A);

// ---- actions ---------------------------------------------------------------
enum ActionId {
    ACT_NONE = 0,
    ACT_COPY_ACCOUNT,
    ACT_TOGGLE_LOCK,
    ACT_CHECKIN,
    ACT_FETCH,
    ACT_OPEN_PORTAL,
    ACT_ENROLL,
    ACT_ENTER_CODE,
    ACT_UNINSTALL,
    ACT_REFRESH,
};

enum ButtonStyle { STYLE_PRIMARY, STYLE_NORMAL, STYLE_DANGER, STYLE_GHOST };

struct Button {
    RECT rc;
    int action;
    std::wstring text;
    int style;
    bool enabled;
};

enum class Conn { Unknown, Checking, Online, Offline };

struct AppModel {
    bool provisioned = false;
    bool lockEnabled = false;
    bool removalProtected = false;
    bool locked = false;
    std::wstring account, name, model, serial, statusText, deadlineText;
    std::wstring portalUrl;
    std::wstring uninstallDeviceToken; // whether we have a token (not shown)
    bool enrolled = false;
    Conn conn = Conn::Unknown;
    std::wstring lastMessage;
};

// ---- globals ---------------------------------------------------------------
HINSTANCE g_inst = nullptr;
HWND g_wnd = nullptr;
AppModel g_model;
std::vector<Button> g_buttons;
int g_hot = -1;

HFONT g_fBrand = nullptr;   // header wordmark
HFONT g_fH1 = nullptr;      // big account number
HFONT g_fLabel = nullptr;   // small caps labels
HFONT g_fValue = nullptr;   // field values
HFONT g_fBtn = nullptr;     // button text
HFONT g_fBadge = nullptr;   // status badge
HICON g_brandIcon = nullptr; // header mark, drawn at 34px from the multi-size .ico

const UINT WM_APP_CONN = WM_APP + 1;   // wParam = Conn

// ---- string helpers --------------------------------------------------------
std::wstring widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}

std::string narrow(const std::wstring& s) {
    if (s.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string a(n > 0 ? n - 1 : 0, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, &a[0], n, nullptr, nullptr);
    return a;
}

// ---- environment -----------------------------------------------------------
std::wstring exeDirectory() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring full(path);
    size_t slash = full.find_last_of(L"\\/");
    return slash == std::wstring::npos ? full : full.substr(0, slash);
}

// zaga_installer sits beside this app in the install folder; during development it
// is in the same build output directory.
std::wstring installerPath() {
    return exeDirectory() + L"\\" + INSTALLER_NAME;
}

// The device store path, honoring the same ZAGA_STATE_PATH test override the gate
// uses, so the whole app reads one consistent store (the machine store in
// production, a temp store when a harness points it elsewhere).
std::wstring storePath() {
    wchar_t override[512];
    DWORD length = GetEnvironmentVariableW(L"ZAGA_STATE_PATH", override, 512);
    if (length > 0 && length < 512) {
        return std::wstring(override, length);
    }
    return LocalStore::defaultPath();
}

// ---- model -----------------------------------------------------------------
void refreshModel() {
    GateInfo info = LockGate::describe();
    g_model.provisioned = info.provisioned;
    g_model.locked = info.locked;
    g_model.account = widen(info.accountNumber);
    g_model.name = widen(info.name);
    g_model.model = widen(info.model);
    g_model.serial = widen(info.serial);
    g_model.statusText = widen(info.statusText);
    g_model.deadlineText = widen(info.deadlineText);
    g_model.lockEnabled = DeviceConfig::lockEnabled();
    g_model.removalProtected = DeviceConfig::uninstallProtected();
    g_model.portalUrl = widen(DeviceConfig::portalUrl());

    StoredDevice device;
    g_model.enrolled = LocalStore::load(storePath(), device) &&
                       !device.deviceToken.empty();
}

// ---- portal check-in (worker thread) ---------------------------------------
DWORD WINAPI checkInThread(LPVOID) {
    Conn result = Conn::Offline;
    std::string portal = DeviceConfig::portalUrl();
    StoredDevice device;
    if (!portal.empty() &&
        LocalStore::load(storePath(), device) &&
        !device.deviceToken.empty()) {
        GateInfo info = LockGate::describe();
        bool ok = PortalClient(portal).heartbeat(device.deviceToken, info.statusText,
                                                 AGENT_VERSION);
        result = ok ? Conn::Online : Conn::Offline;
    } else {
        result = Conn::Unknown;
    }
    PostMessageW(g_wnd, WM_APP_CONN, static_cast<WPARAM>(result), 0);
    return 0;
}

void startCheckIn() {
    if (!g_model.enrolled) {
        g_model.conn = Conn::Unknown;
        return;
    }
    g_model.conn = Conn::Checking;
    HANDLE h = CreateThread(nullptr, 0, checkInThread, nullptr, 0, nullptr);
    if (h) CloseHandle(h);
    InvalidateRect(g_wnd, nullptr, FALSE);
}

// ---- run zaga_installer, elevated, and wait --------------------------------
// Returns the child's exit code, or -1 if it could not be launched. Privileged
// verbs (enable/enroll/uninstall/apply-code/fetch-token) go through here so the
// app never needs to run elevated itself.
DWORD runInstaller(const std::wstring& parameters, bool elevated) {
    std::wstring exe = installerPath();
    if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        g_model.lastMessage = L"zaga_installer.exe was not found next to this app.";
        return static_cast<DWORD>(-1);
    }

    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb = elevated ? L"runas" : L"open";
    info.lpFile = exe.c_str();
    info.lpParameters = parameters.c_str();
    info.nShow = SW_HIDE;

    if (!ShellExecuteExW(&info) || info.hProcess == nullptr) {
        // The user declined the UAC prompt, or launch failed.
        g_model.lastMessage = L"The action was cancelled.";
        return static_cast<DWORD>(-1);
    }

    WaitForSingleObject(info.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(info.hProcess, &code);
    CloseHandle(info.hProcess);
    return code;
}

// Run a privileged verb, refresh, and set a result message.
void doPrivileged(const std::wstring& params, const wchar_t* okMsg,
                  const wchar_t* failMsg) {
    DWORD code = runInstaller(params, true);
    if (code == 0) {
        g_model.lastMessage = okMsg;
    } else if (code != static_cast<DWORD>(-1)) {
        g_model.lastMessage = failMsg;
    }
    refreshModel();
    startCheckIn();
    InvalidateRect(g_wnd, nullptr, FALSE);
}

// ---- clipboard -------------------------------------------------------------
void copyToClipboard(const std::wstring& text) {
    if (!OpenClipboard(g_wnd)) return;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (mem) {
        void* dst = GlobalLock(mem);
        memcpy(dst, text.c_str(), bytes);
        GlobalUnlock(mem);
        SetClipboardData(CF_UNICODETEXT, mem);
    }
    CloseClipboard();
}

// ---------------------------------------------------------------------------
// A small modal text-input dialog, built from controls (no .rc resource). Used
// for the portal URL, enrollment/unlock codes, and the uninstall code.
// ---------------------------------------------------------------------------
struct InputState {
    const wchar_t* prompt;
    std::wstring text;
    bool ok;
    // Set when the dialog has finished, to end the modal loop. The dialog must not
    // post a quit message: WM_QUIT would leak out and terminate the whole app.
    bool done;
    bool password;
    HFONT font;
};

LRESULT CALLBACK inputProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    InputState* st = reinterpret_cast<InputState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_CREATE: {
            CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            st = reinterpret_cast<InputState*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));

            HWND label = CreateWindowW(L"STATIC", st->prompt, WS_CHILD | WS_VISIBLE,
                                       16, 14, 388, 40, hwnd, nullptr, g_inst, nullptr);
            SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(st->font), TRUE);

            DWORD style = WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL;
            if (st->password) style |= ES_PASSWORD;
            HWND edit = CreateWindowW(L"EDIT", st->text.c_str(), style,
                                      16, 58, 388, 26, hwnd, reinterpret_cast<HMENU>(101),
                                      g_inst, nullptr);
            SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(st->font), TRUE);

            HWND ok = CreateWindowW(L"BUTTON", L"OK",
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                    228, 98, 84, 30, hwnd, reinterpret_cast<HMENU>(IDOK),
                                    g_inst, nullptr);
            SendMessageW(ok, WM_SETFONT, reinterpret_cast<WPARAM>(st->font), TRUE);
            HWND cancel = CreateWindowW(L"BUTTON", L"Cancel",
                                        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                        320, 98, 84, 30, hwnd, reinterpret_cast<HMENU>(IDCANCEL),
                                        g_inst, nullptr);
            SendMessageW(cancel, WM_SETFONT, reinterpret_cast<WPARAM>(st->font), TRUE);

            SetFocus(edit);
            SendMessageW(edit, EM_SETSEL, 0, -1);
            return 0;
        }
        // Only flag the result here; inputBox tears the window down after its loop
        // ends, so the parent is re-enabled before the dialog disappears.
        case WM_COMMAND: {
            if (st == nullptr) {
                break;
            }
            int id = LOWORD(wp);
            if (id == IDOK) {
                HWND edit = GetDlgItem(hwnd, 101);
                wchar_t buf[1024];
                GetWindowTextW(edit, buf, 1024);
                st->text = buf;
                st->ok = true;
                st->done = true;
                return 0;
            }
            if (id == IDCANCEL) {
                st->ok = false;
                st->done = true;
                return 0;
            }
            break;
        }
        case WM_CLOSE:
            if (st != nullptr) {
                st->ok = false;
                st->done = true;
            }
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool inputBox(HWND parent, const wchar_t* title, const wchar_t* prompt,
              const std::wstring& def, bool password, std::wstring& out) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = inputProc;
        wc.hInstance = g_inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = L"ZagaInputBox";
        RegisterClassW(&wc);
        registered = true;
    }

    InputState st{};
    st.prompt = prompt;
    st.text = def;
    st.ok = false;
    st.done = false;
    st.password = password;
    st.font = g_fValue;

    RECT pr;
    GetWindowRect(parent, &pr);
    int w = 420, h = 176;
    int x = pr.left + ((pr.right - pr.left) - w) / 2;
    int y = pr.top + ((pr.bottom - pr.top) - h) / 2;

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"ZagaInputBox", title,
                               WS_POPUPWINDOW | WS_CAPTION,
                               x, y, w, h, parent, nullptr, g_inst, &st);
    if (!dlg) return false;

    // Size the client area precisely for the control layout above.
    RECT want{0, 0, 420, 148};
    AdjustWindowRectEx(&want, WS_POPUPWINDOW | WS_CAPTION, FALSE, WS_EX_DLGMODALFRAME);
    SetWindowPos(dlg, nullptr, 0, 0, want.right - want.left, want.bottom - want.top,
                 SWP_NOMOVE | SWP_NOZORDER);

    ShowWindow(dlg, SW_SHOW);
    EnableWindow(parent, FALSE);

    MSG msg;
    while (!st.done) {
        if (!GetMessageW(&msg, nullptr, 0, 0)) {
            // A real WM_QUIT (the app is closing). Put it back so the main loop
            // still sees it, and abandon the prompt.
            PostQuitMessage(static_cast<int>(msg.wParam));
            break;
        }
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    // Re-enable the parent before the dialog goes away, or focus lands on another
    // application instead of the main window.
    EnableWindow(parent, TRUE);
    DestroyWindow(dlg);
    SetActiveWindow(parent);
    out = st.text;
    return st.ok;
}

// ---- action handlers -------------------------------------------------------
void onEnroll() {
    std::wstring url = g_model.portalUrl.empty() ? L"http://" : g_model.portalUrl;
    if (!inputBox(g_wnd, L"Enroll device", L"Portal URL (e.g. http://192.168.1.20/zagatech):",
                  url, false, url) || url.empty()) {
        return;
    }
    std::wstring code;
    if (!inputBox(g_wnd, L"Enroll device",
                  L"One-time enrollment code from the portal:", L"", false, code) ||
        code.empty()) {
        return;
    }
    doPrivileged(L"enroll --url \"" + url + L"\" --code \"" + code + L"\"",
                 L"Device enrolled from the portal.",
                 L"Enrollment failed. Check the URL and code, then try again.");
}

void onEnterCode() {
    std::wstring code;
    if (!inputBox(g_wnd, L"Enter unlock code",
                  L"Unlock code  (XXXXX-XXXXX-XXXXX-XXXXX):", L"", false, code) ||
        code.empty()) {
        return;
    }
    doPrivileged(L"apply-code --code \"" + code + L"\"",
                 L"Code accepted. The device is unlocked.",
                 L"That code was not accepted. The device stays locked.");
}

void onToggleLock() {
    if (g_model.lockEnabled) {
        doPrivileged(L"disable", L"Enforcement disabled. The device will not lock.",
                     L"Could not disable enforcement.");
    } else {
        if (!g_model.provisioned) {
            g_model.lastMessage = L"Enroll the device before arming the lock.";
            InvalidateRect(g_wnd, nullptr, FALSE);
            return;
        }
        doPrivileged(L"enable", L"Enforcement armed. The device locks when overdue.",
                     L"Could not arm enforcement.");
    }
}

void onFetch() {
    doPrivileged(L"fetch-token",
                 L"Pulled the latest unlock code from the portal and applied it.",
                 L"No unlock code was available from the portal.");
}

// If the client was installed with the setup package (Inno), its uninstaller sits
// beside the app as unins###.exe. Prefer it so Add/Remove Programs stays consistent.
std::wstring packageUninstaller() {
    WIN32_FIND_DATAW fd{};
    std::wstring pattern = exeDirectory() + L"\\unins*.exe";
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return std::wstring();
    std::wstring found = exeDirectory() + L"\\" + fd.cFileName;
    FindClose(h);
    return found;
}

void onUninstall() {
    // Package install: hand off to the Windows uninstaller (it runs our unregister
    // step via the package, then removes the files) and close the app.
    std::wstring unins = packageUninstaller();
    if (!unins.empty()) {
        int answer = MessageBoxW(g_wnd,
            L"This opens the Windows uninstaller to remove Zaga Device Lock from "
            L"this device.\n\nContinue?",
            L"Uninstall Zaga Device Lock", MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2);
        if (answer != IDYES) return;
        ShellExecuteW(g_wnd, L"open", unins.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        PostQuitMessage(0);
        return;
    }

    std::wstring code;
    std::wstring params = L"uninstall";
    if (g_model.removalProtected) {
        if (!inputBox(g_wnd, L"Uninstall Zaga",
                      L"Removal is protected. Enter the uninstall code:", L"", false, code) ||
            code.empty()) {
            return;
        }
        params += L" --code \"" + code + L"\"";
    }

    int answer = MessageBoxW(g_wnd,
        L"This removes the Zaga lock from this device: it unregisters the login "
        L"tile, deletes the heartbeat task, and clears all settings.\n\nContinue?",
        L"Uninstall Zaga Device Lock", MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2);
    if (answer != IDYES) return;

    DWORD code2 = runInstaller(params, true);
    if (code2 == 0) {
        MessageBoxW(g_wnd, L"Zaga has been removed from this device.",
                    L"Uninstalled", MB_ICONINFORMATION | MB_OK);
        PostQuitMessage(0);
    } else if (code2 != static_cast<DWORD>(-1)) {
        g_model.lastMessage = g_model.removalProtected
            ? L"Uninstall failed — the code may be wrong."
            : L"Uninstall failed.";
        refreshModel();
        InvalidateRect(g_wnd, nullptr, FALSE);
    }
}

void onOpenPortal() {
    if (g_model.portalUrl.empty()) {
        g_model.lastMessage = L"No portal URL is set. Enroll the device first.";
        InvalidateRect(g_wnd, nullptr, FALSE);
        return;
    }
    ShellExecuteW(g_wnd, L"open", g_model.portalUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void dispatch(int action) {
    switch (action) {
        case ACT_COPY_ACCOUNT:
            if (!g_model.account.empty()) {
                copyToClipboard(g_model.account);
                g_model.lastMessage = L"Account number copied.";
                InvalidateRect(g_wnd, nullptr, FALSE);
            }
            break;
        case ACT_TOGGLE_LOCK: onToggleLock(); break;
        case ACT_CHECKIN:     startCheckIn(); break;
        case ACT_FETCH:       onFetch(); break;
        case ACT_OPEN_PORTAL: onOpenPortal(); break;
        case ACT_ENROLL:      onEnroll(); break;
        case ACT_ENTER_CODE:  onEnterCode(); break;
        case ACT_UNINSTALL:   onUninstall(); break;
        case ACT_REFRESH:
            refreshModel();
            startCheckIn();
            g_model.lastMessage = L"Refreshed.";
            InvalidateRect(g_wnd, nullptr, FALSE);
            break;
    }
}

// ---- drawing helpers -------------------------------------------------------
void fillRound(HDC dc, RECT r, COLORREF fill, COLORREF edge, int radius) {
    HBRUSH br = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, edge);
    HGDIOBJ ob = SelectObject(dc, br);
    HGDIOBJ op = SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(br);
    DeleteObject(pen);
}

void text(HDC dc, const std::wstring& s, RECT r, HFONT font, COLORREF color, UINT fmt) {
    HGDIOBJ of = SelectObject(dc, font);
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    RECT rr = r;
    DrawTextW(dc, s.c_str(), -1, &rr, fmt);
    SelectObject(dc, of);
}

// A labelled field: small caps label over a value.
int field(HDC dc, int x, int y, int w, const wchar_t* label, const std::wstring& value) {
    RECT lr{x, y, x + w, y + 16};
    text(dc, label, lr, g_fLabel, COL_FAINT, DT_LEFT | DT_SINGLELINE);
    RECT vr{x, y + 16, x + w, y + 40};
    std::wstring v = value.empty() ? L"—" : value;
    text(dc, v, vr, g_fValue, COL_TEXT, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    return y + 46;
}

void statusColors(COLORREF& dot, std::wstring& label) {
    if (!g_model.lockEnabled) { dot = COL_ACCENT; label = L"Dormant"; return; }
    if (!g_model.provisioned) { dot = COL_AMBER; label = L"Not provisioned"; return; }
    if (g_model.locked)       { dot = COL_RED;   label = L"Locked"; return; }
    dot = COL_GREEN; label = L"Active";
}

// Register a button rect for hit-testing, and draw it.
void button(HDC dc, RECT r, int action, const std::wstring& label, int style, bool enabled) {
    int idx = static_cast<int>(g_buttons.size());
    g_buttons.push_back({r, action, label, style, enabled});

    bool hot = enabled && idx == g_hot;
    COLORREF fill, edge, fg;
    switch (style) {
        case STYLE_PRIMARY:
            fill = hot ? COL_ACCENT_HOT : COL_ACCENT; edge = fill; fg = RGB(6, 12, 22);
            break;
        case STYLE_DANGER:
            fill = hot ? COL_DANGER_HOT : COL_DANGER; edge = RGB(0x5A, 0x2A, 0x30);
            fg = COL_DANGER_TXT;
            break;
        case STYLE_GHOST:
            fill = hot ? COL_BTN_HOT : COL_INSET; edge = COL_CARD_EDGE; fg = COL_ACCENT;
            break;
        default:
            fill = hot ? COL_BTN_HOT : COL_BTN; edge = COL_CARD_EDGE; fg = COL_TEXT;
            break;
    }
    if (!enabled) { fill = COL_INSET; edge = COL_CARD_EDGE; fg = COL_FAINT; }

    fillRound(dc, r, fill, edge, 10);
    RECT tr = r;
    text(dc, label, tr, g_fBtn, fg, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

// ---- paint -----------------------------------------------------------------
void paint(HWND hwnd, HDC target, const RECT& client) {
    (void)hwnd;
    // Draw into a memory bitmap for flicker-free rendering.
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bmp = CreateCompatibleBitmap(target, client.right, client.bottom);
    HGDIOBJ ob = SelectObject(dc, bmp);

    HBRUSH bg = CreateSolidBrush(COL_BG);
    FillRect(dc, &client, bg);
    DeleteObject(bg);

    g_buttons.clear();

    const int M = 22;               // page margin
    const int W = client.right - M * 2;
    int y = 22;

    // --- header ---
    {
        if (g_brandIcon != nullptr) {
            DrawIconEx(dc, M, y, g_brandIcon, 34, 34, 0, nullptr, DI_NORMAL);
        }
        RECT brand{M + 46, y, M + W, y + 34};
        text(dc, L"ZAGA  DEVICE LOCK", brand, g_fBrand, COL_TEXT,
             DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        // Refresh button top-right
        RECT rb{M + W - 96, y + 3, M + W, y + 31};
        button(dc, rb, ACT_REFRESH, L"Refresh", STYLE_GHOST, true);
    }
    y += 34 + 18;

    // --- status badge row ---
    {
        COLORREF dot; std::wstring lbl;
        statusColors(dot, lbl);
        RECT badge{M, y, M + 200, y + 30};
        // pill
        HBRUSH br = CreateSolidBrush(COL_CARD);
        HPEN pen = CreatePen(PS_SOLID, 1, COL_CARD_EDGE);
        HGDIOBJ o1 = SelectObject(dc, br), o2 = SelectObject(dc, pen);
        RoundRect(dc, badge.left, badge.top, badge.left + 150, badge.bottom, 30, 30);
        SelectObject(dc, o1); SelectObject(dc, o2);
        DeleteObject(br); DeleteObject(pen);
        // dot
        HBRUSH db = CreateSolidBrush(dot);
        HGDIOBJ od = SelectObject(dc, db);
        HPEN np = CreatePen(PS_SOLID, 1, dot);
        HGDIOBJ opn = SelectObject(dc, np);
        Ellipse(dc, badge.left + 16, badge.top + 11, badge.left + 24, badge.top + 19);
        SelectObject(dc, od); SelectObject(dc, opn);
        DeleteObject(db); DeleteObject(np);
        RECT lt{badge.left + 32, badge.top, badge.left + 150, badge.bottom};
        text(dc, lbl, lt, g_fBadge, COL_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    y += 30 + 16;

    // --- device card ---
    {
        int cardTop = y;
        RECT card{M, cardTop, M + W, cardTop + 226};
        fillRound(dc, card, COL_CARD, COL_CARD_EDGE, 14);
        int cx = M + 20;
        int cw = W - 40;
        int cy = cardTop + 18;

        if (!g_model.provisioned) {
            RECT t{cx, cy, cx + cw, cy + 24};
            text(dc, L"This device is not enrolled yet.", t, g_fValue, COL_TEXT,
                 DT_LEFT | DT_SINGLELINE);
            RECT s{cx, cy + 30, cx + cw, cy + 74};
            text(dc, L"Click “Enroll device” below and enter the portal URL and the "
                     L"one-time code from the billing portal.",
                 s, g_fValue, COL_MUTED, DT_LEFT | DT_WORDBREAK);
        } else {
            // account label + big number + copy
            RECT al{cx, cy, cx + cw, cy + 16};
            text(dc, L"ACCOUNT NUMBER", al, g_fLabel, COL_FAINT, DT_LEFT | DT_SINGLELINE);
            RECT an{cx, cy + 16, cx + cw - 80, cy + 56};
            text(dc, g_model.account, an, g_fH1, COL_TEXT, DT_LEFT | DT_SINGLELINE);
            RECT cp{M + W - 92, cy + 20, M + W - 20, cy + 48};
            button(dc, cp, ACT_COPY_ACCOUNT, L"Copy", STYLE_GHOST, true);

            int gy = cy + 66;
            int colw = (cw - 20) / 2;
            int leftY = field(dc, cx, gy, colw, L"DEVICE", g_model.name);
            field(dc, cx + colw + 20, gy, colw, L"MODEL", g_model.model);
            int y2 = leftY;
            int leftY2 = field(dc, cx, y2, colw, L"SERIAL", g_model.serial);
            const wchar_t* dlabel = g_model.locked ? L"LOCKED SINCE" : L"RENEWS / DUE";
            field(dc, cx + colw + 20, y2, colw, dlabel, g_model.deadlineText);
            (void)leftY2;
        }
        y = card.bottom + 14;
    }

    // --- enforcement row ---
    {
        RECT card{M, y, M + W, y + 66};
        fillRound(dc, card, COL_CARD, COL_CARD_EDGE, 14);
        RECT l{M + 20, y + 12, M + W - 160, y + 30};
        text(dc, L"Lock enforcement", l, g_fValue, COL_TEXT, DT_LEFT | DT_SINGLELINE);
        RECT s{M + 20, y + 32, M + W - 160, y + 52};
        std::wstring sub = g_model.lockEnabled
            ? L"Armed — the login screen is gated when overdue."
            : L"Dormant — nothing is gated at the login screen.";
        if (g_model.removalProtected) sub += L"  Removal protected.";
        text(dc, sub, s, g_fLabel, COL_MUTED, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
        RECT b{M + W - 140, y + 17, M + W - 20, y + 49};
        button(dc, b, ACT_TOGGLE_LOCK, g_model.lockEnabled ? L"Disable" : L"Enable",
               g_model.lockEnabled ? STYLE_NORMAL : STYLE_PRIMARY, true);
        y = card.bottom + 14;
    }

    // --- portal card ---
    {
        RECT card{M, y, M + W, y + 128};
        fillRound(dc, card, COL_CARD, COL_CARD_EDGE, 14);
        int cx = M + 20;

        RECT hl{cx, y + 14, M + W - 130, y + 32};
        text(dc, L"BILLING PORTAL", hl, g_fLabel, COL_FAINT, DT_LEFT | DT_SINGLELINE);

        // connection indicator (top-right)
        COLORREF cdot = COL_FAINT; std::wstring ctext = L"Not enrolled";
        switch (g_model.conn) {
            case Conn::Checking: cdot = COL_AMBER; ctext = L"Checking…"; break;
            case Conn::Online:   cdot = COL_GREEN; ctext = L"Online"; break;
            case Conn::Offline:  cdot = COL_RED;   ctext = L"Offline"; break;
            default: break;
        }
        HBRUSH db = CreateSolidBrush(cdot);
        HPEN dp = CreatePen(PS_SOLID, 1, cdot);
        HGDIOBJ od = SelectObject(dc, db), op = SelectObject(dc, dp);
        Ellipse(dc, M + W - 118, y + 18, M + W - 110, y + 26);
        SelectObject(dc, od); SelectObject(dc, op);
        DeleteObject(db); DeleteObject(dp);
        RECT ct{M + W - 104, y + 12, M + W - 16, y + 32};
        text(dc, ctext, ct, g_fLabel, COL_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        RECT url{cx, y + 32, M + W - 20, y + 58};
        std::wstring u = g_model.portalUrl.empty() ? L"Not set" : g_model.portalUrl;
        text(dc, u, url, g_fValue, g_model.portalUrl.empty() ? COL_MUTED : COL_ACCENT,
             DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

        int bw = (W - 40 - 20) / 3;
        int by = y + 76;
        RECT b1{cx, by, cx + bw, by + 34};
        RECT b2{cx + bw + 10, by, cx + bw * 2 + 10, by + 34};
        RECT b3{cx + bw * 2 + 20, by, cx + bw * 3 + 20, by + 34};
        button(dc, b1, ACT_CHECKIN, L"Check in now", STYLE_NORMAL, g_model.enrolled);
        button(dc, b2, ACT_FETCH, L"Get unlock code", STYLE_NORMAL, g_model.enrolled);
        button(dc, b3, ACT_OPEN_PORTAL, L"Open portal", STYLE_NORMAL, !g_model.portalUrl.empty());
        y = card.bottom + 14;
    }

    // --- primary actions ---
    {
        int bw = (W - 20) / 2;
        RECT b1{M, y, M + bw, y + 42};
        RECT b2{M + bw + 20, y, M + W, y + 42};
        button(dc, b1, ACT_ENROLL,
               g_model.provisioned ? L"Re-enroll device" : L"Enroll device",
               STYLE_PRIMARY, true);
        button(dc, b2, ACT_ENTER_CODE, L"Enter unlock code", STYLE_NORMAL,
               g_model.provisioned);
        y = b1.bottom + 12;
        RECT b3{M, y, M + W, y + 40};
        button(dc, b3, ACT_UNINSTALL, L"Uninstall Zaga from this device", STYLE_DANGER, true);
        y = b3.bottom + 16;
    }

    // --- footer / message ---
    {
        RECT sep{M, y, M + W, y + 1};
        HBRUSH sb = CreateSolidBrush(COL_CARD_EDGE);
        FillRect(dc, &sep, sb);
        DeleteObject(sb);
        RECT ver{M, y + 10, M + W, y + 30};
        text(dc, L"Agent " + widen(AGENT_VERSION), ver, g_fLabel, COL_FAINT,
             DT_LEFT | DT_SINGLELINE);
        if (!g_model.lastMessage.empty()) {
            RECT msg{M, y + 10, M + W, y + 30};
            text(dc, g_model.lastMessage, msg, g_fLabel, COL_ACCENT,
                 DT_RIGHT | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
    }

    BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, ob);
    DeleteObject(bmp);
    DeleteDC(dc);
}

int hitTest(int x, int y) {
    POINT p{x, y};
    for (size_t i = 0; i < g_buttons.size(); ++i) {
        if (g_buttons[i].enabled && PtInRect(&g_buttons[i].rc, p)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void makeFonts() {
    auto make = [](int height, int weight, const wchar_t* face) {
        return CreateFontW(height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
    };
    g_fBrand = make(-19, FW_BOLD, L"Segoe UI");
    g_fH1    = make(-34, FW_BOLD, L"Segoe UI");
    g_fLabel = make(-12, FW_SEMIBOLD, L"Segoe UI");
    g_fValue = make(-16, FW_NORMAL, L"Segoe UI");
    g_fBtn   = make(-15, FW_SEMIBOLD, L"Segoe UI");
    g_fBadge = make(-16, FW_SEMIBOLD, L"Segoe UI");
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            makeFonts();
            // Ask for the 34px variant so the header mark is scaled by the icon
            // loader (which picks the nearest embedded size) rather than by DrawIconEx.
            g_brandIcon = static_cast<HICON>(LoadImageW(g_inst, MAKEINTRESOURCEW(IDI_ZAGA),
                                                        IMAGE_ICON, 34, 34, LR_DEFAULTCOLOR));
            refreshModel();
            startCheckIn();
            return 0;
        case WM_APP_CONN:
            g_model.conn = static_cast<Conn>(wp);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_MOUSEMOVE: {
            int hot = hitTest(LOWORD(lp), HIWORD(lp));
            if (hot != g_hot) {
                g_hot = hot;
                SetCursor(LoadCursorW(nullptr, hot >= 0 ? IDC_HAND : IDC_ARROW));
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
            return 0;
        }
        case WM_MOUSELEAVE:
            if (g_hot != -1) { g_hot = -1; InvalidateRect(hwnd, nullptr, FALSE); }
            return 0;
        case WM_LBUTTONUP: {
            int hit = hitTest(LOWORD(lp), HIWORD(lp));
            if (hit >= 0 && hit < static_cast<int>(g_buttons.size())) {
                dispatch(g_buttons[hit].action);
            }
            return 0;
        }
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT && g_hot >= 0) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            break;
        case WM_ERASEBKGND:
            return 1; // handled in WM_PAINT (double-buffered)
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT client;
            GetClientRect(hwnd, &client);
            paint(hwnd, dc, client);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    g_inst = inst;
    SetProcessDPIAware();

    WNDCLASSW wc{};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(COL_BG);
    wc.lpszClassName = APP_CLASS;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_ZAGA));
    RegisterClassW(&wc);

    const int cw = 560, ch = 760;
    RECT r{0, 0, cw, ch};
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&r, style, FALSE);
    int ww = r.right - r.left, wh = r.bottom - r.top;
    int sx = (GetSystemMetrics(SM_CXSCREEN) - ww) / 2;
    int sy = (GetSystemMetrics(SM_CYSCREEN) - wh) / 2;

    g_wnd = CreateWindowExW(0, APP_CLASS, APP_TITLE, style,
                            sx, sy, ww, wh, nullptr, nullptr, inst, nullptr);
    if (!g_wnd) return 1;

    ShowWindow(g_wnd, SW_SHOW);
    UpdateWindow(g_wnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
