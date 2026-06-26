// ===================================================================
// main.cpp — BorderLimited Config GUI (配置工具图形界面)
//
// 本文件是 INI 配置工具的主程序，提供一个可视化的 Windows 对话框
// 用于编辑 BorderLimited.ini 配置文件。
//
// 技术栈: Win32 原生对话框 (DialogBoxParamW) + Visual Studio 对话框
// 编辑器 (BorderLimitedConfig.rc) 进行可视化布局调整。
//
// 三大模块:
//   1. 主对话框 (DlgProc) — 23 个控件的数据绑定和事件处理
//   2. 预览覆盖窗 (PvProc) — 全屏半透明窗口，拖拽矩形定位游戏窗口
//   3. 配置读写 (ConfigLoad/ConfigSave) — INI 文件解析和写入
//
// 功能特性:
//   - 中英双语切换（所有控件文字、提示、保存弹窗均支持）
//   - 每个控件带详细悬浮提示（鼠标悬停 300ms 后显示）
//   - 预览窗口: 拖拽移动/缩放矩形、方向键逐像素微调（按住加速）、
//     Enter/中键居中、Ctrl+S 保存、Esc 取消
//   - PerMonitorV2 DPI 感知（多屏坐标准确）
//   - 单文件 exe（/MT 静态链接 CRT），无需安装运行时库
//
// 编辑布局: 用 Visual Studio 打开 BorderLimitedConfig.sln →
//   双击 .rc 中的 IDD_MAIN → 对话框编辑器直接拖拽控件
// ===================================================================
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include "resource.h"
#include "lang.h"
#include "config_io.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdi32.lib")

static HFONT      g_hFont = nullptr;
static HWND       g_hTooltip = nullptr;
static ConfigData g_Cfg;
static std::wstring g_IniPath;
static std::vector<HMONITOR> g_Monitors;
static int        g_ActiveMonitor = -1;

// ===================================================================
// Monitor list
// ===================================================================
static BOOL CALLBACK MonEnum(HMONITOR m, HDC, LPRECT, LPARAM) { g_Monitors.push_back(m); return TRUE; }
static void RefreshMonitors(HWND hCombo) {
    SendMessageW(hCombo, CB_RESETCONTENT, 0, 0);
    g_Monitors.clear();
    EnumDisplayMonitors(nullptr, nullptr, MonEnum, 0);
    for (size_t i = 0; i < g_Monitors.size(); ++i) {
        MONITORINFO mi = { sizeof(mi) }; GetMonitorInfoW(g_Monitors[i], &mi);
        wchar_t b[128]; swprintf_s(b, L"%s %zu: %d x %d",
            (mi.dwFlags & MONITORINFOF_PRIMARY) ? L"(主)" : L"", i + 1,
            (int)(mi.rcMonitor.right - mi.rcMonitor.left),
            (int)(mi.rcMonitor.bottom - mi.rcMonitor.top));
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)b);
    }
    int sel = (g_ActiveMonitor >= 0 && g_ActiveMonitor < (int)g_Monitors.size()) ? g_ActiveMonitor : 0;
    SendMessageW(hCombo, CB_SETCURSEL, g_Monitors.empty() ? -1 : sel, 0);
}

// ===================================================================
// Tooltip helper
// ===================================================================
static void AddTT(HWND hDlg, int cid, const wchar_t* key) {
    TOOLINFOW ti = { sizeof(ti) };
    ti.uFlags  = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd    = hDlg;
    ti.uId     = (UINT_PTR)GetDlgItem(hDlg, cid);
    ti.lpszText = (LPWSTR)LS(key);
    SendMessageW(g_hTooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
}

// ===================================================================
// Config <-> Controls
// ===================================================================
static void CfgToUI(HWND hDlg) {
    CheckRadioButton(hDlg, IDC_SIZE_FULLSCREEN, IDC_SIZE_BORDERLESS,
        g_Cfg.Size == WindowSize::FullScreen ? IDC_SIZE_FULLSCREEN :
        g_Cfg.Size == WindowSize::Custom   ? IDC_SIZE_CUSTOM : IDC_SIZE_BORDERLESS);

    SetDlgItemInt(hDlg, IDC_POS_X, g_Cfg.PositionX, TRUE);
    SetDlgItemInt(hDlg, IDC_POS_Y, g_Cfg.PositionY, TRUE);
    SetDlgItemInt(hDlg, IDC_POS_W, g_Cfg.PositionW, TRUE);
    SetDlgItemInt(hDlg, IDC_POS_H, g_Cfg.PositionH, TRUE);
    SetDlgItemInt(hDlg, IDC_OFF_L, g_Cfg.OffsetL, TRUE);
    SetDlgItemInt(hDlg, IDC_OFF_T, g_Cfg.OffsetT, TRUE);
    SetDlgItemInt(hDlg, IDC_OFF_R, g_Cfg.OffsetR, TRUE);
    SetDlgItemInt(hDlg, IDC_OFF_B, g_Cfg.OffsetB, TRUE);

    CheckDlgButton(hDlg, IDC_CHK_MAXIMIZE,    g_Cfg.ShouldMaximize);
    CheckDlgButton(hDlg, IDC_CHK_TOPMOST,      g_Cfg.TopMost);
    CheckDlgButton(hDlg, IDC_CHK_REMOVEMENUS,  g_Cfg.RemoveMenus);
    CheckDlgButton(hDlg, IDC_CHK_LOCKCURSOR,   g_Cfg.LockCursor);
    CheckDlgButton(hDlg, IDC_CHK_DISABLEWK,    g_Cfg.DisableWinKey);
    CheckDlgButton(hDlg, IDC_CHK_HIDETASKBAR,  g_Cfg.HideWindowsTaskbar);
    CheckDlgButton(hDlg, IDC_CHK_HIDECURSOR,   g_Cfg.HideMouseCursor);
    CheckDlgButton(hDlg, IDC_CHK_OVERRIDEDPI,  g_Cfg.OverrideDpi);
    CheckDlgButton(hDlg, IDC_CHK_ENABLELOG,    g_Cfg.EnableLog);
    CheckDlgButton(hDlg, IDC_CHK_UE3MODE,      g_Cfg.UE3Mode);

    SetDlgItemInt(hDlg, IDC_POLL,  g_Cfg.PollingIntervalMs, FALSE);
    SetDlgItemInt(hDlg, IDC_DELAY, g_Cfg.DelayMs, FALSE);
    SetDlgItemInt(hDlg, IDC_UE3W,  g_Cfg.UE3RenderWidth, FALSE);
    SetDlgItemInt(hDlg, IDC_UE3H,  g_Cfg.UE3RenderHeight, FALSE);

    EnableWindow(GetDlgItem(hDlg, IDC_UE3W), g_Cfg.UE3Mode);
    EnableWindow(GetDlgItem(hDlg, IDC_UE3H), g_Cfg.UE3Mode);

    bool cust = (g_Cfg.Size == WindowSize::Custom);
    for (int id : {IDC_POS_X, IDC_POS_Y, IDC_POS_W, IDC_POS_H})
        EnableWindow(GetDlgItem(hDlg, id), cust);
}

static void UIToCfg(HWND hDlg) {
    if (IsDlgButtonChecked(hDlg, IDC_SIZE_FULLSCREEN))
        g_Cfg.Size = WindowSize::FullScreen;
    else if (IsDlgButtonChecked(hDlg, IDC_SIZE_CUSTOM))
        g_Cfg.Size = WindowSize::Custom;
    else
        g_Cfg.Size = WindowSize::Borderless;

    g_Cfg.PositionX = (int)GetDlgItemInt(hDlg, IDC_POS_X, nullptr, TRUE);
    g_Cfg.PositionY = (int)GetDlgItemInt(hDlg, IDC_POS_Y, nullptr, TRUE);
    g_Cfg.PositionW = (int)GetDlgItemInt(hDlg, IDC_POS_W, nullptr, TRUE);
    g_Cfg.PositionH = (int)GetDlgItemInt(hDlg, IDC_POS_H, nullptr, TRUE);
    g_Cfg.OffsetL   = (int)GetDlgItemInt(hDlg, IDC_OFF_L, nullptr, TRUE);
    g_Cfg.OffsetT   = (int)GetDlgItemInt(hDlg, IDC_OFF_T, nullptr, TRUE);
    g_Cfg.OffsetR   = (int)GetDlgItemInt(hDlg, IDC_OFF_R, nullptr, TRUE);
    g_Cfg.OffsetB   = (int)GetDlgItemInt(hDlg, IDC_OFF_B, nullptr, TRUE);

    g_Cfg.ShouldMaximize     = IsDlgButtonChecked(hDlg, IDC_CHK_MAXIMIZE);
    g_Cfg.TopMost            = IsDlgButtonChecked(hDlg, IDC_CHK_TOPMOST);
    g_Cfg.RemoveMenus        = IsDlgButtonChecked(hDlg, IDC_CHK_REMOVEMENUS);
    g_Cfg.LockCursor         = IsDlgButtonChecked(hDlg, IDC_CHK_LOCKCURSOR);
    g_Cfg.DisableWinKey      = IsDlgButtonChecked(hDlg, IDC_CHK_DISABLEWK);
    g_Cfg.HideWindowsTaskbar  = IsDlgButtonChecked(hDlg, IDC_CHK_HIDETASKBAR);
    g_Cfg.HideMouseCursor    = IsDlgButtonChecked(hDlg, IDC_CHK_HIDECURSOR);
    g_Cfg.OverrideDpi        = IsDlgButtonChecked(hDlg, IDC_CHK_OVERRIDEDPI);
    g_Cfg.EnableLog          = IsDlgButtonChecked(hDlg, IDC_CHK_ENABLELOG);
    g_Cfg.UE3Mode            = IsDlgButtonChecked(hDlg, IDC_CHK_UE3MODE);

    g_Cfg.PollingIntervalMs = (int)GetDlgItemInt(hDlg, IDC_POLL,  nullptr, FALSE);
    g_Cfg.DelayMs           = (int)GetDlgItemInt(hDlg, IDC_DELAY, nullptr, FALSE);
    g_Cfg.UE3RenderWidth    = (int)GetDlgItemInt(hDlg, IDC_UE3W,  nullptr, FALSE);
    g_Cfg.UE3RenderHeight   = (int)GetDlgItemInt(hDlg, IDC_UE3H,  nullptr, FALSE);
}

// ===================================================================
// Preview overlay — undo/redo, Enter/Ctrl+S save, Esc discard
// ===================================================================
static HWND         g_hPv = nullptr;
static RECT         g_pvR = {};
static RECT         g_pvMon = {};
static int          g_pvMonIdx = 0;
static bool         g_pvDrag = false, g_pvSize = false;
static int          g_pvEdge = 0;
static POINT        g_pvPt = {};
static bool         g_pvSaved = false;

static void ClampToMonitor(RECT& r) {
    if (r.left   < g_pvMon.left)   { int w = r.right - r.left; r.left = g_pvMon.left;    r.right = r.left + w; }
    if (r.top    < g_pvMon.top)    { int h = r.bottom - r.top; r.top  = g_pvMon.top;     r.bottom = r.top + h; }
    if (r.right  > g_pvMon.right)  { int w = r.right - r.left; r.right= g_pvMon.right;   r.left  = r.right - w; }
    if (r.bottom > g_pvMon.bottom) { int h = r.bottom - r.top; r.bottom=g_pvMon.bottom;  r.top   = r.bottom - h; }
}

static void CenterInMonitor(RECT& r) {
    int w = r.right - r.left, h = r.bottom - r.top;
    int monW = g_pvMon.right - g_pvMon.left, monH = g_pvMon.bottom - g_pvMon.top;
    r.left   = g_pvMon.left + (monW - w) / 2;
    r.top    = g_pvMon.top  + (monH - h) / 2;
    r.right  = r.left + w;
    r.bottom = r.top  + h;
}

static LRESULT CALLBACK PvProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        SetWindowLongW(hwnd, GWL_EXSTYLE,
            GetWindowLongW(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED | WS_EX_TOOLWINDOW);
        SetLayeredWindowAttributes(hwnd, 0, 180, LWA_ALPHA);
        SetFocus(hwnd);
        SetTimer(hwnd, 1, 40, nullptr); // 40ms timer for arrow-key repeat
        return 0;
    case WM_TIMER: {
        // Arrow-key repeat with acceleration — initial 200ms delay before timer kicks in
        static DWORD tStart = 0;
        static int lastVK = 0;
        bool any = false;
        for (int vk : {VK_LEFT, VK_RIGHT, VK_UP, VK_DOWN}) {
            if (GetAsyncKeyState(vk) & 0x8000) { any = true; if (vk != lastVK) { tStart = GetTickCount(); lastVK = vk; } break; }
        }
        if (!any) { tStart = 0; lastVK = 0; return 0; }
        DWORD elapsed = GetTickCount() - tStart;
        if (elapsed < 500) return 0; // wait 500ms before starting repeat
        int d = 1;
        if      (elapsed > 5000) d = 10;
        else if (elapsed > 3000) d = 5;
        else if (elapsed > 1500) d = 2;
        if (GetAsyncKeyState(VK_LEFT)  & 0x8000) { g_pvR.left -= d; g_pvR.right -= d; }
        if (GetAsyncKeyState(VK_RIGHT) & 0x8000) { g_pvR.left += d; g_pvR.right += d; }
        if (GetAsyncKeyState(VK_UP)    & 0x8000) { g_pvR.top -= d;  g_pvR.bottom -= d; }
        if (GetAsyncKeyState(VK_DOWN)  & 0x8000) { g_pvR.top += d;  g_pvR.bottom += d; }
        ClampToMonitor(g_pvR);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1; // suppress flicker — we paint everything in WM_PAINT
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { g_pvSaved = false; KillTimer(hwnd,1); DestroyWindow(hwnd); return 0; }
        if (wp == VK_RETURN){
            CenterInMonitor(g_pvR);
            ClampToMonitor(g_pvR);
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }
        // Arrow keys: handled by timer for repeat; do immediate 1px nudge on key-down
        if (wp == VK_LEFT)  { g_pvR.left--; g_pvR.right--; ClampToMonitor(g_pvR); InvalidateRect(hwnd,nullptr,TRUE); return 0; }
        if (wp == VK_RIGHT){ g_pvR.left++; g_pvR.right++; ClampToMonitor(g_pvR); InvalidateRect(hwnd,nullptr,TRUE); return 0; }
        if (wp == VK_UP)   { g_pvR.top--;  g_pvR.bottom--; ClampToMonitor(g_pvR); InvalidateRect(hwnd,nullptr,TRUE); return 0; }
        if (wp == VK_DOWN) { g_pvR.top++;  g_pvR.bottom++; ClampToMonitor(g_pvR); InvalidateRect(hwnd,nullptr,TRUE); return 0; }
        if (wp == 'S' && (GetAsyncKeyState(VK_CONTROL)&0x8000)) {
            g_pvSaved = true; KillTimer(hwnd,1); DestroyWindow(hwnd); return 0;
        }
        break;
    case WM_MBUTTONDOWN:
        CenterInMonitor(g_pvR);
        ClampToMonitor(g_pvR);
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_LBUTTONDOWN: {
        g_pvPt.x = LOWORD(lp); g_pvPt.y = HIWORD(lp);
        const int E = 10; RECT r = g_pvR;
        bool L = (g_pvPt.x >= r.left-E  && g_pvPt.x <= r.left+E);
        bool R = (g_pvPt.x >= r.right-E && g_pvPt.x <= r.right+E);
        bool T = (g_pvPt.y >= r.top-E   && g_pvPt.y <= r.top+E);
        bool B = (g_pvPt.y >= r.bottom-E&& g_pvPt.y <= r.bottom+E);
        if (L||R||T||B) { g_pvSize=true; g_pvEdge=(L?4:0)|(R?8:0)|(T?1:0)|(B?2:0); }
        else if (PtInRect(&r,g_pvPt)) { g_pvDrag=true; }
        else { DestroyWindow(hwnd); return 0; }
        SetCapture(hwnd); return 0;
    }
    case WM_MOUSEMOVE:
        if (!g_pvDrag && !g_pvSize) break;
        { int dx = LOWORD(lp)-g_pvPt.x, dy = HIWORD(lp)-g_pvPt.y;
          if (GetAsyncKeyState(VK_SHIFT)&0x8000) dy=0;
          if (GetAsyncKeyState(VK_CONTROL)&0x8000) dx=0;
          RECT& r = g_pvR;
          if (g_pvDrag) { OffsetRect(&r,dx,dy); }
          else {
            if (g_pvEdge&4) { r.left+=dx;  if(r.left>r.right-80) r.left=r.right-80; }
            if (g_pvEdge&8) { r.right+=dx; if(r.right<r.left+80) r.right=r.left+80; }
            if (g_pvEdge&1) { r.top+=dy;   if(r.top>r.bottom-80) r.top=r.bottom-80; }
            if (g_pvEdge&2) { r.bottom+=dy;if(r.bottom<r.top+80) r.bottom=r.top+80; }
          }
          ClampToMonitor(r);
          g_pvPt.x=LOWORD(lp); g_pvPt.y=HIWORD(lp);
          InvalidateRect(hwnd,nullptr,TRUE); }
        return 0;
    case WM_LBUTTONUP:
        g_pvDrag=g_pvSize=false; ReleaseCapture(); return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
        RECT c; GetClientRect(hwnd, &c);
        int w = c.right - c.left, h = c.bottom - c.top;

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, h);
        HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

        // Background
        HBRUSH bg = CreateSolidBrush(RGB(15,15,15));
        FillRect(memDC, &c, bg); DeleteObject(bg);

        // Monitor outline
        HPEN monPn = CreatePen(PS_DOT, 1, RGB(120,120,120));
        HPEN oldP = (HPEN)SelectObject(memDC, monPn);
        HBRUSH oldB = (HBRUSH)SelectObject(memDC, GetStockObject(NULL_BRUSH));
        Rectangle(memDC, g_pvMon.left, g_pvMon.top, g_pvMon.right, g_pvMon.bottom);

        // Position rectangle
        HPEN pn = CreatePen(PS_SOLID, 3, RGB(255,60,60));
        HBRUSH br = CreateSolidBrush(RGB(200,30,30));
        SelectObject(memDC, pn); SelectObject(memDC, br);
        Rectangle(memDC, g_pvR.left, g_pvR.top, g_pvR.right, g_pvR.bottom);

        // Info text
        SetBkMode(memDC, TRANSPARENT); SetTextColor(memDC, RGB(255,255,255));
        int rw2=g_pvR.right-g_pvR.left, rh2=g_pvR.bottom-g_pvR.top;
        int monW=g_pvMon.right-g_pvMon.left, monH=g_pvMon.bottom-g_pvMon.top;
        wchar_t info[512];
        swprintf_s(info, LS(L"pv_info"),
            rw2, rh2, g_pvR.left, g_pvR.top,
            g_pvMonIdx+1, monW, monH,
            g_pvR.left-g_pvMon.left, g_pvR.top-g_pvMon.top,
            g_pvMon.right-g_pvR.right, g_pvMon.bottom-g_pvR.bottom);
        HFONT oldFont = (HFONT)SelectObject(memDC, g_hFont);
        RECT tr = g_pvR; tr.top += 8;
        DrawTextW(memDC, info, -1, &tr, DT_CENTER | DT_WORDBREAK);
        if (rw2 < 300 || rh2 < 120) {
            RECT br2 = {g_pvMon.left+10, g_pvR.bottom+10, g_pvMon.right-10, g_pvMon.bottom-10};
            DrawTextW(memDC, info, -1, &br2, DT_LEFT | DT_WORDBREAK);
        }

        BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);

        // Cleanup: restore all GDI objects before destroying DC
        SelectObject(memDC, oldFont);
        SelectObject(memDC, oldP); SelectObject(memDC, oldB);
        SelectObject(memDC, oldBmp);
        DeleteObject(pn); DeleteObject(br); DeleteObject(monPn);
        DeleteObject(memBmp); DeleteDC(memDC);
        EndPaint(hwnd, &ps); return 0;
    }
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        if (g_pvSaved) {
            g_Cfg.PositionX=g_pvR.left; g_Cfg.PositionY=g_pvR.top;
            g_Cfg.PositionW=g_pvR.right-g_pvR.left;
            g_Cfg.PositionH=g_pvR.bottom-g_pvR.top;
            g_Cfg.Size=WindowSize::Custom;
            PostMessageW(GetParent(hwnd), WM_APP, 0, 0);
        }
        g_hPv=nullptr;
        g_pvSaved = false;
        break;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}

static void ShowPreview(HWND hParent) {
    if (g_hPv) { DestroyWindow(g_hPv); return; }

    // Choose monitor
    int nMon = (int)g_Monitors.size();
    if (nMon <= 0) return;

    if (nMon == 1) {
        g_pvMonIdx = 0;
    } else {
        if (g_ActiveMonitor >= 0 && g_ActiveMonitor < nMon) {
            g_pvMonIdx = g_ActiveMonitor;
        } else {
            for (int i = 0; i < nMon; ++i) {
                MONITORINFO mi = { sizeof(mi) };
                GetMonitorInfoW(g_Monitors[i], &mi);
                if (mi.dwFlags & MONITORINFOF_PRIMARY) { g_pvMonIdx = i; break; }
            }
        }
    }

    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(g_Monitors[g_pvMonIdx], &mi);
    g_pvMon = mi.rcMonitor;

    if (g_Cfg.PositionW <= 0) g_Cfg.PositionW = 800;
    if (g_Cfg.PositionH <= 0) g_Cfg.PositionH = 600;

    // Clamp initial rect to selected monitor
    g_pvR = { g_Cfg.PositionX, g_Cfg.PositionY,
              g_Cfg.PositionX + g_Cfg.PositionW,
              g_Cfg.PositionY + g_Cfg.PositionH };
    ClampToMonitor(g_pvR);

    int vw = g_pvMon.right - g_pvMon.left;
    int vh = g_pvMon.bottom - g_pvMon.top;

    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc = { sizeof(wc), CS_HREDRAW|CS_VREDRAW, PvProc, 0, 0,
            GetModuleHandleW(nullptr), nullptr, LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW),
            (HBRUSH)(COLOR_WINDOW+1), nullptr, L"BL_Pv4", nullptr };
        RegisterClassExW(&wc); reg = true;
    }
    g_hPv = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"BL_Pv4", L"Preview",
        WS_POPUP | WS_VISIBLE, g_pvMon.left, g_pvMon.top, vw, vh,
        hParent, nullptr, GetModuleHandleW(nullptr), nullptr);
    SetForegroundWindow(g_hPv); SetFocus(g_hPv);
}

// ===================================================================
// Dialog proc
// ===================================================================
static void InitTooltips(HWND hDlg) {
    SendMessageW(g_hTooltip, TTM_DELTOOLW, 0, 0);
    SendMessageW(g_hTooltip, TTM_SETDELAYTIME, TTDT_INITIAL, 300);
    #define TT(id,key) AddTT(hDlg, id, key)
    TT(IDC_SIZE_FULLSCREEN, L"tooltip_size_full");
    TT(IDC_SIZE_CUSTOM,     L"tooltip_size_custom");
    TT(IDC_SIZE_BORDERLESS,   L"tooltip_size_borderless");
    TT(IDC_CHK_MAXIMIZE,    L"tooltip_maximize");
    TT(IDC_CHK_TOPMOST,     L"tooltip_topmost");
    TT(IDC_CHK_REMOVEMENUS, L"tooltip_removemenus");
    TT(IDC_CHK_LOCKCURSOR,  L"tooltip_lockcursor");
    TT(IDC_CHK_DISABLEWK,   L"tooltip_disablewinkey");
    TT(IDC_CHK_HIDETASKBAR, L"tooltip_hidetaskbar");
    TT(IDC_CHK_HIDECURSOR,  L"tooltip_hidecursor");
    TT(IDC_CHK_OVERRIDEDPI, L"tooltip_overridedpi");
    TT(IDC_CHK_ENABLELOG,   L"tooltip_enablelog");
    TT(IDC_CHK_UE3MODE,     L"tooltip_ue3mode");
    TT(IDC_POLL,             L"tooltip_poll");
    TT(IDC_DELAY,            L"tooltip_delay");
    TT(IDC_BTN_PREVIEW,      L"tooltip_preview");
    #undef TT
}

static void RefreshLanguage(HWND hDlg) {
    SetWindowTextW(hDlg, LS(L"title"));
    // Radio buttons
    SetDlgItemTextW(hDlg, IDC_SIZE_FULLSCREEN, LS(L"fullscreen"));
    SetDlgItemTextW(hDlg, IDC_SIZE_CUSTOM,     LS(L"custom"));
    SetDlgItemTextW(hDlg, IDC_SIZE_BORDERLESS,   LS(L"borderless"));
    // Group boxes
    SetDlgItemTextW(hDlg, IDC_GRP_MODE, LS(L"grp_mode"));
    SetDlgItemTextW(hDlg, IDC_GRP_POS,  LS(L"grp_position"));
    SetDlgItemTextW(hDlg, IDC_GRP_OFF,  LS(L"grp_offset"));
    SetDlgItemTextW(hDlg, IDC_GRP_BEH,  LS(L"grp_behavior"));
    SetDlgItemTextW(hDlg, IDC_GRP_ADV,  LS(L"grp_advanced"));
    // Checkboxes
    SetDlgItemTextW(hDlg, IDC_CHK_MAXIMIZE,    LS(L"should_maximize"));
    SetDlgItemTextW(hDlg, IDC_CHK_TOPMOST,     LS(L"top_most"));
    SetDlgItemTextW(hDlg, IDC_CHK_REMOVEMENUS, LS(L"remove_menus"));
    SetDlgItemTextW(hDlg, IDC_CHK_LOCKCURSOR,  LS(L"lock_cursor"));
    SetDlgItemTextW(hDlg, IDC_CHK_DISABLEWK,   LS(L"disable_winkey"));
    SetDlgItemTextW(hDlg, IDC_CHK_HIDETASKBAR, LS(L"hide_taskbar"));
    SetDlgItemTextW(hDlg, IDC_CHK_HIDECURSOR,  LS(L"hide_cursor"));
    SetDlgItemTextW(hDlg, IDC_CHK_OVERRIDEDPI, LS(L"override_dpi"));
    SetDlgItemTextW(hDlg, IDC_CHK_ENABLELOG,   LS(L"enable_log"));
    SetDlgItemTextW(hDlg, IDC_CHK_UE3MODE,     LS(L"ue3_mode"));
    // Static labels
    SetDlgItemTextW(hDlg, IDC_LBL_POLL,    LS(L"lbl_poll"));
    SetDlgItemTextW(hDlg, IDC_LBL_DELAY,   LS(L"lbl_delay"));
    SetDlgItemTextW(hDlg, IDC_LBL_UE3RES,  LS(L"lbl_ue3res"));
    SetDlgItemTextW(hDlg, IDC_LBL_MONITOR, LS(L"lbl_monitor"));
    // Buttons
    SetDlgItemTextW(hDlg, IDC_BTN_PREVIEW,  LS(L"btn_preview"));
    SetDlgItemTextW(hDlg, IDC_BTN_SAVE,     LS(L"btn_save"));
    SetDlgItemTextW(hDlg, IDC_BTN_DEFAULTS, LS(L"btn_defaults"));
    SetDlgItemTextW(hDlg, IDC_BTN_LANG,     LS(L"lang_toggle"));
    SetDlgItemTextW(hDlg, IDC_BTN_EXTENDED, LS(L"btn_extended"));
    InitTooltips(hDlg);
}

// ===================================================================
// 扩展选项对话框 — 热键设置等
// ===================================================================
static INT_PTR CALLBACK ExtDlgProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        // 运行时设置多语言文本（.rc 中为空，避免编码问题）
        SetWindowTextW(hDlg, LS(L"title_extended"));
        SetDlgItemTextW(hDlg, -1, LS(L"grp_hotkey"));
        SetDlgItemTextW(hDlg, IDC_CHK_HOTKEY_ENABLE, LS(L"hotkey_enable"));
        SetDlgItemTextW(hDlg, IDOK, LS(L"btn_ok"));
        SetDlgItemTextW(hDlg, IDCANCEL, LS(L"btn_cancel"));
        SetDlgItemTextW(hDlg, IDC_LBL_POLL,  LS(L"lbl_hotkey_code"));
        SetDlgItemTextW(hDlg, IDC_LBL_DELAY, LS(L"lbl_hotkey_mod"));
        // 居中
        RECT rc; GetWindowRect(hDlg, &rc);
        int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
        SetWindowPos(hDlg, nullptr, (sw-(rc.right-rc.left))/2, (sh-(rc.bottom-rc.top))/2,
            0,0, SWP_NOSIZE|SWP_NOZORDER);
        // 加载当前热键配置
        CheckDlgButton(hDlg, IDC_CHK_HOTKEY_ENABLE, g_Cfg.ToggleHotKey);
        SetDlgItemInt(hDlg, IDC_HOTKEY_CODE, g_Cfg.ToggleHotKeyCode, FALSE);
        EnableWindow(GetDlgItem(hDlg, IDC_HOTKEY_CODE), g_Cfg.ToggleHotKey);
        // 修饰键单选
        int modId = IDC_HOTKEY_MOD_ALT;
        if (g_Cfg.ToggleHotKeyMod == MOD_CONTROL) modId = IDC_HOTKEY_MOD_CTRL;
        else if (g_Cfg.ToggleHotKeyMod == MOD_SHIFT) modId = IDC_HOTKEY_MOD_SHIFT;
        else if (g_Cfg.ToggleHotKeyMod == MOD_WIN) modId = IDC_HOTKEY_MOD_WIN;
        CheckRadioButton(hDlg, IDC_HOTKEY_MOD_ALT, IDC_HOTKEY_MOD_WIN, modId);
        // 启用/禁用相关控件
        for (int id : {IDC_HOTKEY_CODE, IDC_HOTKEY_MOD_ALT, IDC_HOTKEY_MOD_CTRL,
                       IDC_HOTKEY_MOD_SHIFT, IDC_HOTKEY_MOD_WIN})
            EnableWindow(GetDlgItem(hDlg, id), g_Cfg.ToggleHotKey);
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            // 保存到 g_Cfg
            g_Cfg.ToggleHotKey = IsDlgButtonChecked(hDlg, IDC_CHK_HOTKEY_ENABLE);
            g_Cfg.ToggleHotKeyCode = (int)GetDlgItemInt(hDlg, IDC_HOTKEY_CODE, nullptr, FALSE);
            if (IsDlgButtonChecked(hDlg, IDC_HOTKEY_MOD_CTRL))  g_Cfg.ToggleHotKeyMod = MOD_CONTROL;
            else if (IsDlgButtonChecked(hDlg, IDC_HOTKEY_MOD_SHIFT)) g_Cfg.ToggleHotKeyMod = MOD_SHIFT;
            else if (IsDlgButtonChecked(hDlg, IDC_HOTKEY_MOD_WIN))   g_Cfg.ToggleHotKeyMod = MOD_WIN;
            else g_Cfg.ToggleHotKeyMod = MOD_ALT;
            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        if (LOWORD(wp) == IDCANCEL) { EndDialog(hDlg, IDCANCEL); return TRUE; }
        if (LOWORD(wp) == IDC_CHK_HOTKEY_ENABLE && HIWORD(wp) == BN_CLICKED) {
            bool en = IsDlgButtonChecked(hDlg, IDC_CHK_HOTKEY_ENABLE);
            for (int id : {IDC_HOTKEY_CODE, IDC_HOTKEY_MOD_ALT, IDC_HOTKEY_MOD_CTRL,
                           IDC_HOTKEY_MOD_SHIFT, IDC_HOTKEY_MOD_WIN})
                EnableWindow(GetDlgItem(hDlg, id), en);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static INT_PTR CALLBACK DlgProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        // App icon
        HICON hIcon = LoadIconW((HINSTANCE)GetWindowLongPtrW(hDlg, GWLP_HINSTANCE), MAKEINTRESOURCEW(IDI_MAIN));
        SendMessageW(hDlg, WM_SETICON, ICON_BIG,   (LPARAM)hIcon);
        SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);

        // Center on screen
        RECT rc; GetWindowRect(hDlg, &rc);
        int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
        SetWindowPos(hDlg, nullptr, (sw-(rc.right-rc.left))/2, (sh-(rc.bottom-rc.top))/2,
            0,0, SWP_NOSIZE|SWP_NOZORDER);

        g_hFont = (HFONT)SendMessageW(hDlg, WM_GETFONT, 0, 0);
        for (HWND h = GetWindow(hDlg, GW_CHILD); h; h = GetWindow(h, GW_HWNDNEXT))
            SendMessageW(h, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        // Tooltips
        g_hTooltip = CreateWindowExW(0, TOOLTIPS_CLASSW, nullptr, WS_POPUP|TTS_ALWAYSTIP,
            0,0,0,0, hDlg, nullptr, (HINSTANCE)GetWindowLongPtrW(hDlg, GWLP_HINSTANCE), nullptr);
        InitTooltips(hDlg);

        RefreshMonitors(GetDlgItem(hDlg, IDC_MONITOR));
        RefreshLanguage(hDlg);
        CfgToUI(hDlg);
        return TRUE;
    }
    case WM_COMMAND: {
        WORD id = LOWORD(wp), ev = HIWORD(wp);
        if (id == IDC_BTN_SAVE && ev == BN_CLICKED) {
            UIToCfg(hDlg);
            ConfigSave(g_IniPath.c_str(), g_Cfg);
            MessageBoxW(hDlg, LS(L"msg_saved"), LS(L"title"), MB_OK | MB_ICONINFORMATION);
        }
        else if (id == IDC_BTN_DEFAULTS && ev == BN_CLICKED) {
            g_Cfg = ConfigData{};
            CfgToUI(hDlg);
        }
        else if (id == IDC_BTN_LANG && ev == BN_CLICKED) {
            ToggleLang();
            RefreshLanguage(hDlg);
        }
        else if (id == IDC_BTN_EXTENDED && ev == BN_CLICKED) {
            UIToCfg(hDlg);                                          // 同步当前 UI 到 g_Cfg
            HINSTANCE hI = (HINSTANCE)GetWindowLongPtrW(hDlg, GWLP_HINSTANCE);
            DialogBoxParamW(hI, MAKEINTRESOURCEW(IDD_EXTENDED), hDlg, ExtDlgProc, 0);
            CfgToUI(hDlg);                                          // 同步 g_Cfg 回 UI
        }
        else if (id == IDC_BTN_PREVIEW && ev == BN_CLICKED) {
            UIToCfg(hDlg);
            ShowPreview(hDlg);
            // Preview writes to g_Cfg on close, then posts WM_APP → CfgToUI
        }
        else if ((id == IDC_CHK_UE3MODE || id == IDC_SIZE_FULLSCREEN ||
                  id == IDC_SIZE_CUSTOM || id == IDC_SIZE_BORDERLESS) && ev == BN_CLICKED) {
            UIToCfg(hDlg); CfgToUI(hDlg);
        }
        else if (id == IDC_MONITOR && ev == CBN_SELCHANGE) {
            int sel = (int)SendMessageW(GetDlgItem(hDlg, IDC_MONITOR), CB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < (int)g_Monitors.size()) {
                g_ActiveMonitor = sel;
                MONITORINFO mi = { sizeof(mi) }; GetMonitorInfoW(g_Monitors[sel], &mi);
                g_Cfg.PositionX = mi.rcMonitor.left;
                g_Cfg.PositionY = mi.rcMonitor.top;
                g_Cfg.PositionW = mi.rcMonitor.right  - mi.rcMonitor.left;
                g_Cfg.PositionH = mi.rcMonitor.bottom - mi.rcMonitor.top;
                g_Cfg.Size = WindowSize::Custom;
                CfgToUI(hDlg);
            }
        }
        return TRUE;
    }
    case WM_APP:
        // Preview window closed — refresh position fields
        CfgToUI(hDlg);
        return TRUE;
    case WM_CLOSE:
        EndDialog(hDlg, 0);
        return TRUE;
    }
    return FALSE;
}

// ===================================================================
// WinMain
// ===================================================================
int APIENTRY wWinMain(HINSTANCE hI, HINSTANCE, LPWSTR, int nShow) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    InitCommonControls();

    wchar_t ep[MAX_PATH]; GetModuleFileNameW(nullptr, ep, MAX_PATH);
    wchar_t* s = wcsrchr(ep, L'\\'); if (s) *s = L'\0';
    g_IniPath = std::wstring(ep) + L"\\BorderLimited.ini";
    ConfigLoad(g_IniPath.c_str(), g_Cfg);

    DialogBoxParamW(hI, MAKEINTRESOURCEW(IDD_MAIN), nullptr, DlgProc, 0);
    return 0;
}
