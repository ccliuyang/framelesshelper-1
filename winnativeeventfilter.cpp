/*
 * MIT License
 *
 * Copyright (C) 2020 by wangwenx190 (Yuhang Zhao)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "winnativeeventfilter.h"

#include <QDebug>
#include <QGuiApplication>
#include <QLibrary>
#include <QOperatingSystemVersion>
#ifdef QT_QUICK_LIB
#include <QQuickItem>
#endif
#ifdef QT_WIDGETS_LIB
#include <QWidget>
#endif
#include <QtMath>

// All the following constants and macros are copied from Windows 10 SDK
// directly, without any modifications.

#ifdef IsMinimized
#undef IsMinimized
#endif

// Only available since Windows 2000
#define IsMinimized m_lpIsIconic

#ifdef IsMaximized
#undef IsMaximized
#endif

// Only available since Windows 2000
#define IsMaximized m_lpIsZoomed

#ifndef GET_X_LPARAM
// Only available since Windows 2000
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#endif

#ifndef GET_Y_LPARAM
// Only available since Windows 2000
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#endif

#ifndef GetStockBrush
// Only available since Windows 2000
#define GetStockBrush(i) ((HBRUSH)m_lpGetStockObject(i))
#endif

#ifndef ABM_GETSTATE
// Only available since Windows XP
#define ABM_GETSTATE 0x00000004
#endif

#ifndef ABM_GETTASKBARPOS
// Only available since Windows XP
#define ABM_GETTASKBARPOS 0x00000005
#endif

#ifndef ABS_AUTOHIDE
// Only available since Windows XP
#define ABS_AUTOHIDE 0x0000001
#endif

#ifndef ABE_LEFT
// Only available since Windows XP
#define ABE_LEFT 0
#endif

#ifndef ABE_TOP
// Only available since Windows XP
#define ABE_TOP 1
#endif

#ifndef ABE_RIGHT
// Only available since Windows XP
#define ABE_RIGHT 2
#endif

#ifndef ABE_BOTTOM
// Only available since Windows XP
#define ABE_BOTTOM 3
#endif

#ifndef BPPF_ERASE
// Only available since Windows Vista
#define BPPF_ERASE 0x0001
#endif

#ifndef BPPF_NOCLIP
// Only available since Windows Vista
#define BPPF_NOCLIP 0x0002
#endif

#ifndef USER_DEFAULT_SCREEN_DPI
// Only available since Windows Vista
#define USER_DEFAULT_SCREEN_DPI 96
#endif

#ifndef SM_CXPADDEDBORDER
// Only available since Windows Vista
#define SM_CXPADDEDBORDER 92
#endif

#ifndef WM_NCUAHDRAWCAPTION
// Not documented, only available since Windows Vista
#define WM_NCUAHDRAWCAPTION 0x00AE
#endif

#ifndef WM_NCUAHDRAWFRAME
// Not documented, only available since Windows Vista
#define WM_NCUAHDRAWFRAME 0x00AF
#endif

#ifndef WM_DWMCOMPOSITIONCHANGED
// Only available since Windows Vista
#define WM_DWMCOMPOSITIONCHANGED 0x031E
#endif

#ifndef WM_DPICHANGED
// Only available since Windows 8.1
#define WM_DPICHANGED 0x02E0
#endif

#ifndef ABM_GETAUTOHIDEBAREX
// Only available since Windows 8.1
#define ABM_GETAUTOHIDEBAREX 0x0000000b
#endif

#ifndef WNEF_GENERATE_WINAPI
#define WNEF_GENERATE_WINAPI(funcName, resultType, ...)                        \
    using _WNEF_WINAPI_##funcName = resultType(WINAPI *)(__VA_ARGS__);         \
    _WNEF_WINAPI_##funcName m_lp##funcName = nullptr;
#endif

#ifndef WNEF_RESOLVE_WINAPI
#define WNEF_RESOLVE_WINAPI(libName, funcName)                                 \
    if (!m_lp##funcName) {                                                     \
        QLibrary library(QString::fromUtf8(#libName));                         \
        m_lp##funcName = reinterpret_cast<_WNEF_WINAPI_##funcName>(            \
            library.resolve(#funcName));                                       \
        Q_ASSERT_X(m_lp##funcName, __FUNCTION__,                               \
                   qUtf8Printable(library.errorString()));                     \
    }
#endif

namespace {

// All the following enums, structs and function prototypes are copied from
// Windows 10 SDK directly, without any modifications.

const UINT m_defaultDotsPerInch = USER_DEFAULT_SCREEN_DPI;

const qreal m_defaultDevicePixelRatio = 1.0;

int m_borderWidth = -1, m_borderHeight = -1, m_titlebarHeight = -1;

using HPAINTBUFFER = HANDLE;

using MONITOR_DPI_TYPE = enum _MONITOR_DPI_TYPE { MDT_EFFECTIVE_DPI = 0 };

using DWMNCRENDERINGPOLICY = enum _DWMNCRENDERINGPOLICY { DWMNCRP_ENABLED = 2 };

using DWMWINDOWATTRIBUTE = enum _DWMWINDOWATTRIBUTE {
    DWMWA_NCRENDERING_POLICY = 2,
    DWMWA_EXTENDED_FRAME_BOUNDS = 9
};

using MARGINS = struct _MARGINS {
    int cxLeftWidth;
    int cxRightWidth;
    int cyTopHeight;
    int cyBottomHeight;
};

using APPBARDATA = struct _APPBARDATA {
    DWORD cbSize;
    HWND hWnd;
    UINT uCallbackMessage;
    UINT uEdge;
    RECT rc;
    LPARAM lParam;
};

using PROCESS_DPI_AWARENESS = enum _PROCESS_DPI_AWARENESS {
    PROCESS_DPI_UNAWARE = 0,
    PROCESS_SYSTEM_DPI_AWARE = 1,
    PROCESS_PER_MONITOR_DPI_AWARE = 2
};

using BP_BUFFERFORMAT = enum _BP_BUFFERFORMAT { BPBF_TOPDOWNDIB = 2 };

using BLENDFUNCTION = struct _BLENDFUNCTION {
    BYTE BlendOp;
    BYTE BlendFlags;
    BYTE SourceConstantAlpha;
    BYTE AlphaFormat;
};

using BP_PAINTPARAMS = struct _BP_PAINTPARAMS {
    DWORD cbSize;
    DWORD dwFlags;
    CONST RECT *prcExclude;
    CONST BLENDFUNCTION *pBlendFunction;
};

// Some of the following functions are not used by this code anymore,
// but we don't remove them completely because we may still need them later.
WNEF_GENERATE_WINAPI(GetSystemDpiForProcess, UINT, HANDLE)
WNEF_GENERATE_WINAPI(GetDpiForWindow, UINT, HWND)
WNEF_GENERATE_WINAPI(GetDpiForSystem, UINT)
WNEF_GENERATE_WINAPI(GetSystemMetricsForDpi, int, int, UINT)
WNEF_GENERATE_WINAPI(GetDpiForMonitor, HRESULT, HMONITOR, MONITOR_DPI_TYPE,
                     UINT *, UINT *)
WNEF_GENERATE_WINAPI(DwmExtendFrameIntoClientArea, HRESULT, HWND,
                     CONST MARGINS *)
WNEF_GENERATE_WINAPI(DwmIsCompositionEnabled, HRESULT, BOOL *)
WNEF_GENERATE_WINAPI(DwmSetWindowAttribute, HRESULT, HWND, DWORD, LPCVOID,
                     DWORD)
WNEF_GENERATE_WINAPI(SHAppBarMessage, UINT_PTR, DWORD, APPBARDATA *)
WNEF_GENERATE_WINAPI(GetDeviceCaps, int, HDC, int)
WNEF_GENERATE_WINAPI(DefWindowProcW, LRESULT, HWND, UINT, WPARAM, LPARAM)
WNEF_GENERATE_WINAPI(SetLayeredWindowAttributes, BOOL, HWND, COLORREF, BYTE,
                     DWORD)
WNEF_GENERATE_WINAPI(MoveWindow, BOOL, HWND, int, int, int, int, BOOL)
WNEF_GENERATE_WINAPI(IsZoomed, BOOL, HWND)
WNEF_GENERATE_WINAPI(IsIconic, BOOL, HWND)
WNEF_GENERATE_WINAPI(GetSystemMetrics, int, int)
WNEF_GENERATE_WINAPI(GetDC, HDC, HWND)
WNEF_GENERATE_WINAPI(ReleaseDC, int, HWND, HDC)
WNEF_GENERATE_WINAPI(RedrawWindow, BOOL, HWND, CONST RECT *, HRGN, UINT)
WNEF_GENERATE_WINAPI(GetClientRect, BOOL, HWND, LPRECT)
WNEF_GENERATE_WINAPI(GetWindowRect, BOOL, HWND, LPRECT)
WNEF_GENERATE_WINAPI(ScreenToClient, BOOL, HWND, LPPOINT)
WNEF_GENERATE_WINAPI(EqualRect, BOOL, CONST RECT *, CONST RECT *)
#ifdef Q_PROCESSOR_X86_64
WNEF_GENERATE_WINAPI(GetWindowLongPtrW, LONG_PTR, HWND, int)
WNEF_GENERATE_WINAPI(SetWindowLongPtrW, LONG_PTR, HWND, int, LONG_PTR)
WNEF_GENERATE_WINAPI(GetClassLongPtrW, ULONG_PTR, HWND, int)
WNEF_GENERATE_WINAPI(SetClassLongPtrW, ULONG_PTR, HWND, int, LONG_PTR)
#else
#ifdef LONG_PTR
#undef LONG_PTR
#endif
#define LONG_PTR LONG
WNEF_GENERATE_WINAPI(GetWindowLongW, LONG_PTR, HWND, int)
WNEF_GENERATE_WINAPI(SetWindowLongW, LONG_PTR, HWND, int, LONG_PTR)
#define m_lpGetWindowLongPtrW m_lpGetWindowLongW
#define m_lpSetWindowLongPtrW m_lpSetWindowLongW
#ifdef GWLP_USERDATA
#undef GWLP_USERDATA
#endif
#define GWLP_USERDATA GWL_USERDATA
WNEF_GENERATE_WINAPI(GetClassLongW, DWORD, HWND, int)
WNEF_GENERATE_WINAPI(SetClassLongW, DWORD, HWND, int, LONG_PTR)
#define m_lpGetClassLongPtrW m_lpGetClassLongW
#define m_lpSetClassLongPtrW m_lpSetClassLongW
#ifdef GCLP_HBRBACKGROUND
#undef GCLP_HBRBACKGROUND
#endif
#define GCLP_HBRBACKGROUND GCL_HBRBACKGROUND
#endif
WNEF_GENERATE_WINAPI(FindWindowW, HWND, LPCWSTR, LPCWSTR)
WNEF_GENERATE_WINAPI(MonitorFromWindow, HMONITOR, HWND, DWORD)
WNEF_GENERATE_WINAPI(GetMonitorInfoW, BOOL, HMONITOR, LPMONITORINFO)
WNEF_GENERATE_WINAPI(GetAncestor, HWND, HWND, UINT)
WNEF_GENERATE_WINAPI(GetDesktopWindow, HWND)
WNEF_GENERATE_WINAPI(SendMessageW, LRESULT, HWND, UINT, WPARAM, LPARAM)
WNEF_GENERATE_WINAPI(SetWindowPos, BOOL, HWND, HWND, int, int, int, int, UINT)
WNEF_GENERATE_WINAPI(UpdateWindow, BOOL, HWND)
WNEF_GENERATE_WINAPI(InvalidateRect, BOOL, HWND, CONST RECT *, BOOL)
WNEF_GENERATE_WINAPI(SetWindowRgn, int, HWND, HRGN, BOOL)
WNEF_GENERATE_WINAPI(IsWindow, BOOL, HWND)
WNEF_GENERATE_WINAPI(GetWindowInfo, BOOL, HWND, LPWINDOWINFO)
WNEF_GENERATE_WINAPI(CreateSolidBrush, HBRUSH, COLORREF)
WNEF_GENERATE_WINAPI(FillRect, int, HDC, CONST RECT *, HBRUSH)
WNEF_GENERATE_WINAPI(DeleteObject, BOOL, HGDIOBJ)
WNEF_GENERATE_WINAPI(IsThemeActive, BOOL)
WNEF_GENERATE_WINAPI(BeginPaint, HDC, HWND, LPPAINTSTRUCT)
WNEF_GENERATE_WINAPI(EndPaint, BOOL, HWND, CONST PAINTSTRUCT *)
WNEF_GENERATE_WINAPI(GetCurrentProcess, HANDLE)
WNEF_GENERATE_WINAPI(GetProcessDpiAwareness, HRESULT, HANDLE,
                     PROCESS_DPI_AWARENESS *)
WNEF_GENERATE_WINAPI(IsProcessDPIAware, BOOL)
#if 0
WNEF_GENERATE_WINAPI(D2D1CreateFactory, HRESULT, D2D1_FACTORY_TYPE, REFIID,
                     CONST D2D1_FACTORY_OPTIONS *, void **)
#endif
WNEF_GENERATE_WINAPI(AdjustWindowRectEx, BOOL, LPRECT, DWORD, BOOL, DWORD)
WNEF_GENERATE_WINAPI(AdjustWindowRectExForDpi, BOOL, LPRECT, DWORD, BOOL, DWORD,
                     UINT)
WNEF_GENERATE_WINAPI(DwmDefWindowProc, BOOL, HWND, UINT, WPARAM, LPARAM,
                     LRESULT *)
WNEF_GENERATE_WINAPI(DwmGetWindowAttribute, HRESULT, HWND, DWORD, PVOID, DWORD)
WNEF_GENERATE_WINAPI(GetStockObject, HGDIOBJ, int)
WNEF_GENERATE_WINAPI(BufferedPaintSetAlpha, HRESULT, HPAINTBUFFER, CONST RECT *,
                     BYTE)
WNEF_GENERATE_WINAPI(EndBufferedPaint, HRESULT, HPAINTBUFFER, BOOL)
WNEF_GENERATE_WINAPI(BeginBufferedPaint, HPAINTBUFFER, HDC, CONST RECT *,
                     BP_BUFFERFORMAT, BP_PAINTPARAMS *, HDC *)
WNEF_GENERATE_WINAPI(CreateRectRgnIndirect, HRGN, CONST RECT *)
WNEF_GENERATE_WINAPI(GetDCEx, HDC, HWND, HRGN, DWORD)
WNEF_GENERATE_WINAPI(GetWindowDC, HDC, HWND)
WNEF_GENERATE_WINAPI(OffsetRect, BOOL, LPRECT, int, int)

// Some APIs are not available on old systems, so we will load them
// dynamically at run-time to get maximum compatibility.
void ResolveWin32APIs() {
    static bool resolved = false;
    if (resolved) {
        // Don't resolve twice.
        return;
    }
    resolved = true;
    // Available since Windows 2000.
    WNEF_RESOLVE_WINAPI(User32, OffsetRect)
    WNEF_RESOLVE_WINAPI(User32, GetWindowDC)
    WNEF_RESOLVE_WINAPI(User32, GetDCEx)
    WNEF_RESOLVE_WINAPI(User32, AdjustWindowRectEx)
    WNEF_RESOLVE_WINAPI(User32, EndPaint)
    WNEF_RESOLVE_WINAPI(User32, BeginPaint)
    WNEF_RESOLVE_WINAPI(User32, FillRect)
    WNEF_RESOLVE_WINAPI(User32, GetWindowInfo)
    WNEF_RESOLVE_WINAPI(User32, IsWindow)
    WNEF_RESOLVE_WINAPI(User32, SetWindowRgn)
    WNEF_RESOLVE_WINAPI(User32, InvalidateRect)
    WNEF_RESOLVE_WINAPI(User32, UpdateWindow)
    WNEF_RESOLVE_WINAPI(User32, SetWindowPos)
    WNEF_RESOLVE_WINAPI(User32, SendMessageW)
    WNEF_RESOLVE_WINAPI(User32, GetDesktopWindow)
    WNEF_RESOLVE_WINAPI(User32, GetAncestor)
    WNEF_RESOLVE_WINAPI(User32, DefWindowProcW)
    WNEF_RESOLVE_WINAPI(User32, SetLayeredWindowAttributes)
    WNEF_RESOLVE_WINAPI(User32, MoveWindow)
    WNEF_RESOLVE_WINAPI(User32, IsZoomed)
    WNEF_RESOLVE_WINAPI(User32, IsIconic)
    WNEF_RESOLVE_WINAPI(User32, GetSystemMetrics)
    WNEF_RESOLVE_WINAPI(User32, GetDC)
    WNEF_RESOLVE_WINAPI(User32, ReleaseDC)
    WNEF_RESOLVE_WINAPI(User32, RedrawWindow)
    WNEF_RESOLVE_WINAPI(User32, GetClientRect)
    WNEF_RESOLVE_WINAPI(User32, GetWindowRect)
    WNEF_RESOLVE_WINAPI(User32, ScreenToClient)
    WNEF_RESOLVE_WINAPI(User32, EqualRect)
#ifdef Q_PROCESSOR_X86_64
    // These functions only exist in 64 bit User32.dll
    WNEF_RESOLVE_WINAPI(User32, GetWindowLongPtrW)
    WNEF_RESOLVE_WINAPI(User32, SetWindowLongPtrW)
    WNEF_RESOLVE_WINAPI(User32, GetClassLongPtrW)
    WNEF_RESOLVE_WINAPI(User32, SetClassLongPtrW)
#else
    WNEF_RESOLVE_WINAPI(User32, GetWindowLongW)
    WNEF_RESOLVE_WINAPI(User32, SetWindowLongW)
    WNEF_RESOLVE_WINAPI(User32, GetClassLongW)
    WNEF_RESOLVE_WINAPI(User32, SetClassLongW)
#endif
    WNEF_RESOLVE_WINAPI(User32, FindWindowW)
    WNEF_RESOLVE_WINAPI(User32, MonitorFromWindow)
    WNEF_RESOLVE_WINAPI(User32, GetMonitorInfoW)
    WNEF_RESOLVE_WINAPI(Gdi32, GetDeviceCaps)
    WNEF_RESOLVE_WINAPI(Gdi32, CreateSolidBrush)
    WNEF_RESOLVE_WINAPI(Gdi32, DeleteObject)
    WNEF_RESOLVE_WINAPI(Gdi32, GetStockObject)
    WNEF_RESOLVE_WINAPI(Gdi32, CreateRectRgnIndirect)
    // Available since Windows XP.
    WNEF_RESOLVE_WINAPI(Shell32, SHAppBarMessage)
    WNEF_RESOLVE_WINAPI(Kernel32, GetCurrentProcess)
    // Available since Windows Vista.
    WNEF_RESOLVE_WINAPI(User32, IsProcessDPIAware)
    WNEF_RESOLVE_WINAPI(Dwmapi, DwmGetWindowAttribute)
    WNEF_RESOLVE_WINAPI(Dwmapi, DwmIsCompositionEnabled)
    WNEF_RESOLVE_WINAPI(Dwmapi, DwmExtendFrameIntoClientArea)
    WNEF_RESOLVE_WINAPI(Dwmapi, DwmSetWindowAttribute)
    WNEF_RESOLVE_WINAPI(Dwmapi, DwmDefWindowProc)
    WNEF_RESOLVE_WINAPI(UxTheme, IsThemeActive)
    WNEF_RESOLVE_WINAPI(UxTheme, BufferedPaintSetAlpha)
    WNEF_RESOLVE_WINAPI(UxTheme, EndBufferedPaint)
    WNEF_RESOLVE_WINAPI(UxTheme, BeginBufferedPaint)
    // Available since Windows 7.
#if 0
    WNEF_RESOLVE_WINAPI(D2D1, D2D1CreateFactory)
#endif
    // Available since Windows 8.1
    if (QOperatingSystemVersion::current() >=
        QOperatingSystemVersion::Windows8_1) {
        WNEF_RESOLVE_WINAPI(SHCore, GetDpiForMonitor)
        WNEF_RESOLVE_WINAPI(SHCore, GetProcessDpiAwareness)
    }
    // Available since Windows 10, version 1607 (10.0.14393)
    if (QOperatingSystemVersion::current() >=
        QOperatingSystemVersion(QOperatingSystemVersion::Windows, 10, 0,
                                14393)) {
        WNEF_RESOLVE_WINAPI(User32, GetDpiForWindow)
        WNEF_RESOLVE_WINAPI(User32, GetDpiForSystem)
        WNEF_RESOLVE_WINAPI(User32, GetSystemMetricsForDpi)
        WNEF_RESOLVE_WINAPI(User32, AdjustWindowRectExForDpi)
    }
    // Available since Windows 10, version 1803 (10.0.17134)
    if (QOperatingSystemVersion::current() >=
        QOperatingSystemVersion(QOperatingSystemVersion::Windows, 10, 0,
                                17134)) {
        WNEF_RESOLVE_WINAPI(User32, GetSystemDpiForProcess)
    }
}

BOOL IsDwmCompositionEnabled() {
    // Since Win8, DWM composition is always enabled and can't be disabled.
    // In other words, DwmIsCompositionEnabled will always return TRUE on
    // systems newer than Win7.
    BOOL enabled = FALSE;
    return SUCCEEDED(m_lpDwmIsCompositionEnabled(&enabled)) && enabled;
}

WINDOWINFO GetInfoForWindow(HWND handle) {
    WINDOWINFO windowInfo;
    SecureZeroMemory(&windowInfo, sizeof(windowInfo));
    windowInfo.cbSize = sizeof(windowInfo);
    if (handle && m_lpIsWindow(handle)) {
        m_lpGetWindowInfo(handle, &windowInfo);
    }
    return windowInfo;
}

MONITORINFO GetMonitorInfoForWindow(HWND handle) {
    MONITORINFO monitorInfo;
    SecureZeroMemory(&monitorInfo, sizeof(monitorInfo));
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (handle && m_lpIsWindow(handle)) {
        const HMONITOR monitor =
            m_lpMonitorFromWindow(handle, MONITOR_DEFAULTTONEAREST);
        if (monitor) {
            m_lpGetMonitorInfoW(monitor, &monitorInfo);
        }
    }
    return monitorInfo;
}

BOOL IsFullScreen(HWND handle) {
    if (handle && m_lpIsWindow(handle)) {
        const WINDOWINFO windowInfo = GetInfoForWindow(handle);
        const MONITORINFO monitorInfo = GetMonitorInfoForWindow(handle);
        // The only way to judge whether a window is fullscreen or not
        // is to compare it's size with the screen's size, there is no official
        // Win32 API to do this for us.
        return m_lpEqualRect(&windowInfo.rcWindow, &monitorInfo.rcMonitor) ||
            m_lpEqualRect(&windowInfo.rcClient, &monitorInfo.rcMonitor);
    }
    return FALSE;
}

BOOL IsTopLevel(HWND handle) {
    if (handle && m_lpIsWindow(handle)) {
        if (m_lpGetWindowLongPtrW(handle, GWL_STYLE) & WS_CHILD) {
            return FALSE;
        }
        const HWND parent = m_lpGetAncestor(handle, GA_PARENT);
        if (parent && (parent != m_lpGetDesktopWindow())) {
            return FALSE;
        }
        return TRUE;
    }
    return FALSE;
}

UINT GetDotsPerInchForWindow(HWND handle) {
    const auto getScreenDpi = [](UINT defaultValue) -> UINT {
#if 0
        if (m_lpD2D1CreateFactory) {
            // Using Direct2D to get the screen DPI.
            // Available since Windows 7.
            ID2D1Factory *m_pDirect2dFactory = nullptr;
            if (SUCCEEDED(m_lpD2D1CreateFactory(
                    D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory),
                    nullptr, reinterpret_cast<void **>(&m_pDirect2dFactory))) &&
                m_pDirect2dFactory) {
                m_pDirect2dFactory->ReloadSystemMetrics();
                FLOAT dpiX = defaultValue, dpiY = defaultValue;
                m_pDirect2dFactory->GetDesktopDpi(&dpiX, &dpiY);
                // The values of *dpiX and *dpiY are identical.
                return qRound(dpiX);
            }
        }
#endif
        // Available since Windows 2000.
        const HDC hdc = m_lpGetDC(nullptr);
        if (hdc) {
            const int dpiX = m_lpGetDeviceCaps(hdc, LOGPIXELSX);
            const int dpiY = m_lpGetDeviceCaps(hdc, LOGPIXELSY);
            m_lpReleaseDC(nullptr, hdc);
            // The values of dpiX and dpiY are identical actually, just to
            // silence a compiler warning.
            return dpiX == dpiY ? dpiY : dpiX;
        }
        return defaultValue;
    };
    bool dpiEnabled = false;
    if (m_lpGetProcessDpiAwareness) {
        PROCESS_DPI_AWARENESS awareness = PROCESS_DPI_UNAWARE;
        m_lpGetProcessDpiAwareness(m_lpGetCurrentProcess(), &awareness);
        dpiEnabled = awareness != PROCESS_DPI_UNAWARE;
    } else if (m_lpIsProcessDPIAware) {
        dpiEnabled = m_lpIsProcessDPIAware();
    }
    if (!dpiEnabled) {
        // Return hard-coded DPI if DPI scaling is disabled.
        return m_defaultDotsPerInch;
    }
    if (!m_lpIsWindow(handle)) {
        if (m_lpGetSystemDpiForProcess) {
            return m_lpGetSystemDpiForProcess(m_lpGetCurrentProcess());
        } else if (m_lpGetDpiForSystem) {
            return m_lpGetDpiForSystem();
        }
        return getScreenDpi(m_defaultDotsPerInch);
    }
    if (m_lpGetDpiForWindow) {
        return m_lpGetDpiForWindow(handle);
    }
    if (m_lpGetDpiForMonitor) {
        UINT dpiX = m_defaultDotsPerInch, dpiY = m_defaultDotsPerInch;
        m_lpGetDpiForMonitor(
            m_lpMonitorFromWindow(handle, MONITOR_DEFAULTTONEAREST),
            MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
        // The values of *dpiX and *dpiY are identical.
        return dpiX;
    }
    return getScreenDpi(m_defaultDotsPerInch);
}

qreal GetPreferedNumber(qreal num) {
    qreal result = -1.0;
    const auto getRoundedNumber = [](qreal in) -> qreal {
        // If the given number is not very large, we assume it's a
        // device pixel ratio (DPR), otherwise we assume it's a DPI.
        if (in < m_defaultDotsPerInch) {
            return qRound(in);
        } else {
            if (in < (m_defaultDotsPerInch * 1.5)) {
                return m_defaultDotsPerInch;
            } else if (in == (m_defaultDotsPerInch * 1.5)) {
                return m_defaultDotsPerInch * 1.5;
            } else if (in < (m_defaultDotsPerInch * 2.5)) {
                return m_defaultDotsPerInch * 2;
            } else if (in == (m_defaultDotsPerInch * 2.5)) {
                return m_defaultDotsPerInch * 2.5;
            } else if (in < (m_defaultDotsPerInch * 3.5)) {
                return m_defaultDotsPerInch * 3;
            } else if (in == (m_defaultDotsPerInch * 3.5)) {
                return m_defaultDotsPerInch * 3.5;
            } else if (in < (m_defaultDotsPerInch * 4.5)) {
                return m_defaultDotsPerInch * 4;
            } else {
                qWarning().noquote()
                    << "DPI too large:" << static_cast<int>(in);
            }
        }
        return -1.0;
    };
#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
    switch (QGuiApplication::highDpiScaleFactorRoundingPolicy()) {
    case Qt::HighDpiScaleFactorRoundingPolicy::PassThrough:
        // Default behavior for Qt 6.
        result = num;
        break;
    case Qt::HighDpiScaleFactorRoundingPolicy::Floor:
        result = qFloor(num);
        break;
    case Qt::HighDpiScaleFactorRoundingPolicy::Ceil:
        result = qCeil(num);
        break;
    default:
        // Default behavior for Qt 5.6 to 5.15
        result = getRoundedNumber(num);
        break;
    }
#else
    // Default behavior for Qt 5.6 to 5.15
    result = getRoundedNumber(num);
#endif
    return result;
}

qreal GetDevicePixelRatioForWindow(HWND handle) {
    qreal result = m_defaultDevicePixelRatio;
    if (handle && m_lpIsWindow(handle)) {
        result = static_cast<qreal>(GetDotsPerInchForWindow(handle)) /
            static_cast<qreal>(m_defaultDotsPerInch);
    }
    return GetPreferedNumber(result);
}

RECT GetFrameSizeForWindow(HWND handle, bool includingTitleBar = false) {
    RECT rect = {0, 0, 0, 0};
    if (handle && m_lpIsWindow(handle)) {
        const auto style = m_lpGetWindowLongPtrW(handle, GWL_STYLE);
        // It's the same with using GetSystemMetrics, the returned values
        // of the two functions are identical.
        if (m_lpAdjustWindowRectExForDpi) {
            m_lpAdjustWindowRectExForDpi(
                &rect,
                includingTitleBar ? (style | WS_CAPTION)
                                  : (style & ~WS_CAPTION),
                FALSE, m_lpGetWindowLongPtrW(handle, GWL_EXSTYLE),
                GetDotsPerInchForWindow(handle));
        } else {
            m_lpAdjustWindowRectEx(&rect,
                                   includingTitleBar ? (style | WS_CAPTION)
                                                     : (style & ~WS_CAPTION),
                                   FALSE,
                                   m_lpGetWindowLongPtrW(handle, GWL_EXSTYLE));
            const qreal dpr = GetDevicePixelRatioForWindow(handle);
            rect.top = qRound(rect.top * dpr);
            rect.bottom = qRound(rect.bottom * dpr);
            rect.left = qRound(rect.left * dpr);
            rect.right = qRound(rect.right * dpr);
        }
        // Some values may be negative. Make them positive unconditionally.
        rect.top = qAbs(rect.top);
        rect.bottom = qAbs(rect.bottom);
        rect.left = qAbs(rect.left);
        rect.right = qAbs(rect.right);
    }
    return rect;
}

void UpdateFrameMarginsForWindow(HWND handle) {
    if (handle && m_lpIsWindow(handle)) {
        MARGINS margins = {0, 0, 0, 0};
        if (IsDwmCompositionEnabled()) {
            // The frame shadow is drawn on the non-client area and thus we have
            // to make sure the non-client area rendering is enabled first.
            const DWMNCRENDERINGPOLICY ncrp = DWMNCRP_ENABLED;
            m_lpDwmSetWindowAttribute(handle, DWMWA_NCRENDERING_POLICY, &ncrp,
                                      sizeof(ncrp));
            // Use negative values have the same effect, however, it will
            // cause the window become transparent when it's maximizing or
            // restoring from maximized. Just like flashing. Fixing it by
            // passing positive values.
            // The system won't draw the frame shadow if the window doesn't
            // have a frame, so we have to extend the frame a bit to let the
            // system draw the shadow. We won't see any frame even we have
            // extended it because we have turned the whole window area into
            // the client area in WM_NCCALCSIZE so we won't see it due to
            // it's covered by the client area (in other words, it's still
            // there, we just can't see it).
            margins.cyTopHeight = 1;
        }
        m_lpDwmExtendFrameIntoClientArea(handle, &margins);
    }
}

int GetSystemMetricsForWindow(HWND handle, int index) {
    if (handle && m_lpIsWindow(handle)) {
        if (m_lpGetSystemMetricsForDpi) {
            return m_lpGetSystemMetricsForDpi(
                index,
                static_cast<UINT>(qRound(
                    GetPreferedNumber(GetDotsPerInchForWindow(handle)))));
        } else {
            return qRound(m_lpGetSystemMetrics(index) *
                          GetDevicePixelRatioForWindow(handle));
        }
    }
    return -1;
}

void createUserData(HWND handle,
                    const WinNativeEventFilter::WINDOWDATA *data = nullptr) {
    if (handle && m_lpIsWindow(handle)) {
        const auto userData = reinterpret_cast<WinNativeEventFilter::WINDOW *>(
            m_lpGetWindowLongPtrW(handle, GWLP_USERDATA));
        if (userData) {
            if (data) {
                userData->windowData = *data;
            }
        } else {
            // Yes, this is a memory leak, but it doesn't hurt much, unless your
            // application has thousands of windows.
            WinNativeEventFilter::WINDOW *_data =
                new WinNativeEventFilter::WINDOW;
            _data->hWnd = handle;
            if (data) {
                _data->windowData = *data;
            }
            m_lpSetWindowLongPtrW(handle, GWLP_USERDATA,
                                  reinterpret_cast<LONG_PTR>(_data));
            WinNativeEventFilter::updateWindow(handle, true, false);
        }
    }
}

// The thickness of an auto-hide taskbar in pixels.
const int kAutoHideTaskbarThicknessPx = 2;
const int kAutoHideTaskbarThicknessPy = kAutoHideTaskbarThicknessPx;

QScopedPointer<WinNativeEventFilter> m_instance;

QVector<HWND> m_framelessWindows;

} // namespace

WinNativeEventFilter::WinNativeEventFilter() { ResolveWin32APIs(); }

WinNativeEventFilter::~WinNativeEventFilter() = default;

void WinNativeEventFilter::install() {
    if (m_instance.isNull()) {
        m_instance.reset(new WinNativeEventFilter);
        qApp->installNativeEventFilter(m_instance.data());
    }
}

void WinNativeEventFilter::uninstall() {
    if (!m_instance.isNull()) {
        qApp->removeNativeEventFilter(m_instance.data());
        m_instance.reset();
    }
    if (!m_framelessWindows.isEmpty()) {
        m_framelessWindows.clear();
    }
}

QVector<HWND> WinNativeEventFilter::framelessWindows() {
    return m_framelessWindows;
}

void WinNativeEventFilter::setFramelessWindows(QVector<HWND> windows) {
    if (!windows.isEmpty() && (windows != m_framelessWindows)) {
        m_framelessWindows = windows;
        for (auto &&window : qAsConst(m_framelessWindows)) {
            createUserData(window);
        }
        install();
    }
}

void WinNativeEventFilter::addFramelessWindow(HWND window,
                                              const WINDOWDATA *data,
                                              bool center, int x, int y,
                                              int width, int height) {
    ResolveWin32APIs();
    if (window && m_lpIsWindow(window) &&
        !m_framelessWindows.contains(window)) {
        m_framelessWindows.append(window);
        createUserData(window, data);
        install();
    }
    if ((x > 0) && (y > 0) && (width > 0) && (height > 0)) {
        setWindowGeometry(window, x, y, width, height);
    }
    if (center) {
        moveWindowToDesktopCenter(window);
    }
}

void WinNativeEventFilter::removeFramelessWindow(HWND window) {
    if (window && m_framelessWindows.contains(window)) {
        m_framelessWindows.removeAll(window);
    }
}

void WinNativeEventFilter::clearFramelessWindows() {
    if (!m_framelessWindows.isEmpty()) {
        m_framelessWindows.clear();
    }
}

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
bool WinNativeEventFilter::nativeEventFilter(const QByteArray &eventType,
                                             void *message, qintptr *result)
#else
bool WinNativeEventFilter::nativeEventFilter(const QByteArray &eventType,
                                             void *message, long *result)
#endif
{
    // The example code in Qt's documentation has this check. I don't know
    // whether we really need this check or not, but adding this check won't
    // bring us harm anyway.
    if (eventType == "windows_generic_MSG") {
#if (QT_VERSION == QT_VERSION_CHECK(5, 11, 1))
        // Work-around a bug caused by typo which only exists in Qt 5.11.1
        const auto msg = *reinterpret_cast<MSG **>(message);
#else
        const auto msg = static_cast<LPMSG>(message);
#endif
        if (!msg || (msg && !msg->hwnd)) {
            // Why sometimes the window handle is null? Is it designed to be?
            // Anyway, we should skip it in this case.
            return false;
        }
        if (m_framelessWindows.isEmpty()) {
            // Only top level windows can be frameless.
            if (!IsTopLevel(msg->hwnd)) {
                return false;
            }
        } else if (!m_framelessWindows.contains(msg->hwnd)) {
            return false;
        }
        const auto data = reinterpret_cast<WINDOW *>(
            m_lpGetWindowLongPtrW(msg->hwnd, GWLP_USERDATA));
        if (!data) {
            // Work-around a long existing Windows bug.
            // Overlapped windows will receive a WM_GETMINMAXINFO message before
            // WM_NCCREATE. This is safe to ignore. It doesn't need any special
            // handling anyway.
            if (msg->message == WM_NCCREATE) {
                const auto userData =
                    reinterpret_cast<LPCREATESTRUCTW>(msg->lParam)
                        ->lpCreateParams;
                m_lpSetWindowLongPtrW(msg->hwnd, GWLP_USERDATA,
                                      reinterpret_cast<LONG_PTR>(userData));
                // Copied from MSDN without any modification:
                // If you have changed certain window data using SetWindowLong,
                // you must call SetWindowPos for the changes to take effect.
                // Use the following combination for uFlags: SWP_NOMOVE |
                // SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED.
                updateWindow(msg->hwnd, true, false);
            }
            *result = m_lpDefWindowProcW(msg->hwnd, msg->message, msg->wParam,
                                         msg->lParam);
            return false;
        }
        if (!data->initialized) {
            // Avoid initializing a same window twice.
            data->initialized = TRUE;
            // Restore default window style.
            // WS_OVERLAPPEDWINDOW = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
            // WS_THICKFRAME |  WS_MINIMIZEBOX | WS_MAXIMIZEBOX
            // Apply the WS_OVERLAPPEDWINDOW window style to restore the window
            // to a normal native Win32 window.
            // Don't apply the Qt::FramelessWindowHint flag, it will add the
            // WS_POPUP window style to the window, which will turn the window
            // into a popup window, losing all the functions a normal window
            // should have.
            // WS_CLIPCHILDREN | WS_CLIPSIBLINGS: work-around strange bugs.
            m_lpSetWindowLongPtrW(msg->hwnd, GWL_STYLE,
                                  WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN |
                                      WS_CLIPSIBLINGS);
            if (!data->windowData.notLayeredWindow) {
                // Turn our window into a layered window to get better
                // performance and hopefully, to get rid of some strange bugs at
                // the same time. But this will break the Arcylic effect
                // (introduced in Win10 1709), if you use the undocumented API
                // SetWindowCompositionAttribute to enable it for this window,
                // the whole window will become totally black. Don't know why
                // currently.
                m_lpSetWindowLongPtrW(msg->hwnd, GWL_EXSTYLE,
                                      WS_EX_APPWINDOW | WS_EX_LAYERED);
                // A layered window can't be visible unless we call
                // SetLayeredWindowAttributes or UpdateLayeredWindow once.
                m_lpSetLayeredWindowAttributes(msg->hwnd, RGB(255, 0, 255), 0,
                                               LWA_COLORKEY);
            }
            // Trigger a frame change event to let us enter the WM_NCCALCSIZE
            // message to remove our title bar as early as possible.
            updateWindow(msg->hwnd, true, false);
        }
        switch (msg->message) {
        case WM_NCCALCSIZE: {
            // Windows是根据这个消息的返回值来设置窗口的客户区（窗口中真正显示的内容）
            // 和非客户区（标题栏、窗口边框、菜单栏和状态栏等Windows系统自行提供的部分
            // ，不过对于Qt来说，除了标题栏和窗口边框，非客户区基本也都是自绘的）的范
            // 围的，lParam里存放的就是新客户区的几何区域，默认是整个窗口的大小，正常
            // 的程序需要修改这个参数，告知系统窗口的客户区和非客户区的范围（一般来说可
            // 以完全交给Windows，让其自行处理，使用默认的客户区和非客户区），因此如果
            // 我们不修改lParam，就可以使客户区充满整个窗口，从而去掉标题栏和窗口边框
            // （因为这些东西都被客户区给盖住了。但边框阴影也会因此而丢失，不过我们会使
            // 用其他方式将其带回，请参考其他消息的处理，此处不过多提及）。但有个情况要
            // 特别注意，那就是窗口最大化后，窗口的实际尺寸会比屏幕的尺寸大一点，从而使
            // 用户看不到窗口的边界，这样用户就不能在窗口最大化后调整窗口的大小了（虽然
            // 这个做法听起来特别奇怪，但Windows确实就是这样做的），因此如果我们要自行
            // 处理窗口的非客户区，就要在窗口最大化后，将窗口边框的宽度和高度（一般是相
            // 等的）从客户区裁剪掉，否则我们窗口所显示的内容就会超出屏幕边界，显示不全。
            // 如果用户开启了任务栏自动隐藏，在窗口最大化后，还要考虑任务栏的位置。因为
            // 如果窗口最大化后，其尺寸和屏幕尺寸相等（因为任务栏隐藏了，所以窗口最大化
            // 后其实是充满了整个屏幕，变相的全屏了），Windows会认为窗口已经进入全屏的
            // 状态，从而导致自动隐藏的任务栏无法弹出。要避免这个状况，就要使窗口的尺寸
            // 小于屏幕尺寸。我下面的做法参考了火狐、Chromium和Windows Terminal
            // 如果没有开启任务栏自动隐藏，是不存在这个问题的，所以要先进行判断。
            // 一般情况下，*result设置为0（相当于DefWindowProc的返回值为0）就可以了，
            // 根据MSDN的说法，返回0意为此消息已经被程序自行处理了，让Windows跳过此消
            // 息，否则Windows会添加对此消息的默认处理，对于当前这个消息而言，就意味着
            // 标题栏和窗口边框又会回来，这当然不是我们想要的结果。根据MSDN，当wParam
            // 为FALSE时，只能返回0，但当其为TRUE时，可以返回0，也可以返回一个WVR_常
            // 量。根据Chromium的注释，当存在非客户区时，如果返回WVR_REDRAW会导致子
            // 窗口/子控件出现奇怪的bug（自绘控件错位），并且Lucas在Windows 10
            // 上成功复现，说明这个bug至今都没有解决。我查阅了大量资料，发现唯一的解决
            // 方案就是返回0。但如果不存在非客户区，且wParam为TRUE，最好返回
            // WVR_REDRAW，否则窗口在调整大小可能会产生严重的闪烁现象。
            // 虽然对大多数消息来说，返回0都代表让Windows忽略此消息，但实际上不同消息
            // 能接受的返回值是不一样的，请注意自行查阅MSDN。

            // Sent when the size and position of a window's client area must be
            // calculated. By processing this message, an application can
            // control the content of the window's client area when the size or
            // position of the window changes. If wParam is TRUE, lParam points
            // to an NCCALCSIZE_PARAMS structure that contains information an
            // application can use to calculate the new size and position of the
            // client rectangle. If wParam is FALSE, lParam points to a RECT
            // structure. On entry, the structure contains the proposed window
            // rectangle for the window. On exit, the structure should contain
            // the screen coordinates of the corresponding window client area.
            // The client area is the window's content area, the non-client area
            // is the area which is provided by the system, such as the title
            // bar, the four window borders, the frame shadow, the menu bar, the
            // status bar, the scroll bar, etc. But for Qt, it draws most of the
            // window area (client + non-client) itself. We now know that the
            // title bar and the window frame is in the non-client area and we
            // can set the scope of the client area in this message, so we can
            // remove the title bar and the window frame by let the non-client
            // area be covered by the client area (because we can't really get
            // rid of the non-client area, it will always be there, all we can
            // do is to hide it) , which means we should let the client area's
            // size the same with the whole window's size. So there is no room
            // for the non-client area and then the user won't be able to see it
            // again. But how to achieve this? Very easy, just leave lParam (the
            // re-calculated client area) untouched. But of course you can
            // modify lParam, then the non-client area will be seen and the
            // window borders and the window frame will show up. However, things
            // are quite different when you try to modify the top margin of the
            // client area. DWM will always draw the whole title bar no matter
            // what margin value you set for the top, unless you don't modify it
            // and remove the whole top area (the title bar + the one pixel
            // height window border). This can be confirmed in Windows
            // Terminal's source code, you can also try yourself to verify
            // it. So things will become quite complicated if you want to
            // preserve the four window borders. So we just remove the whole
            // window frame, otherwise the code will become much more complex.
            const auto mode = static_cast<BOOL>(msg->wParam);
            // If the window bounds change, we're going to relayout and repaint
            // anyway. Returning WVR_REDRAW avoids an extra paint before that of
            // the old client pixels in the (now wrong) location, and thus makes
            // actions like resizing a window from the left edge look slightly
            // less broken.
            *result = mode ? WVR_REDRAW : 0;
            const auto clientRect = mode
                ? &(reinterpret_cast<LPNCCALCSIZE_PARAMS>(msg->lParam)->rgrc[0])
                : reinterpret_cast<LPRECT>(msg->lParam);
            // We don't need this correction when we're fullscreen. We will
            // have the WS_POPUP size, so we don't have to worry about
            // borders, and the default frame will be fine.
            if (IsMaximized(msg->hwnd) && !IsFullScreen(msg->hwnd)) {
                // Windows automatically adds a standard width border to all
                // sides when a window is maximized. We have to remove it
                // otherwise the content of our window will be cut-off from
                // the screen.
                // The value of border width and border height should be
                // identical in most cases, when the scale factor is 1.0, it
                // should be eight pixels.
                const int bw =
                    getSystemMetric(msg->hwnd, SystemMetric::BorderWidth);
                const int bh =
                    getSystemMetric(msg->hwnd, SystemMetric::BorderHeight);
                clientRect->top += bh;
                clientRect->bottom -= bh;
                clientRect->left += bw;
                clientRect->right -= bw;
            }
            // Attempt to detect if there's an autohide taskbar, and if
            // there is, reduce our size a bit on the side with the taskbar,
            // so the user can still mouse-over the taskbar to reveal it.
            // Make sure to use MONITOR_DEFAULTTONEAREST, so that this will
            // still find the right monitor even when we're restoring from
            // minimized.
            if (IsMaximized(msg->hwnd)) {
                APPBARDATA abd;
                SecureZeroMemory(&abd, sizeof(abd));
                abd.cbSize = sizeof(abd);
                const UINT taskbarState =
                    m_lpSHAppBarMessage(ABM_GETSTATE, &abd);
                // First, check if we have an auto-hide taskbar at all:
                if (taskbarState & ABS_AUTOHIDE) {
                    bool top = false, bottom = false, left = false,
                         right = false;
                    // Due to ABM_GETAUTOHIDEBAREX only exists from Win8.1,
                    // we have to use another way to judge this if we are
                    // running on Windows 7 or Windows 8.
                    if (QOperatingSystemVersion::current() >=
                        QOperatingSystemVersion::Windows8_1) {
                        const MONITORINFO monitorInfo =
                            GetMonitorInfoForWindow(msg->hwnd);
                        // This helper can be used to determine if there's a
                        // auto-hide taskbar on the given edge of the monitor
                        // we're currently on.
                        const auto hasAutohideTaskbar =
                            [&monitorInfo](const UINT edge) -> bool {
                            APPBARDATA _abd;
                            SecureZeroMemory(&_abd, sizeof(_abd));
                            _abd.cbSize = sizeof(_abd);
                            _abd.uEdge = edge;
                            _abd.rc = monitorInfo.rcMonitor;
                            const auto hTaskbar =
                                reinterpret_cast<HWND>(m_lpSHAppBarMessage(
                                    ABM_GETAUTOHIDEBAREX, &_abd));
                            return hTaskbar != nullptr;
                        };
                        top = hasAutohideTaskbar(ABE_TOP);
                        bottom = hasAutohideTaskbar(ABE_BOTTOM);
                        left = hasAutohideTaskbar(ABE_LEFT);
                        right = hasAutohideTaskbar(ABE_RIGHT);
                    } else {
                        // The following code is copied from Mozilla Firefox,
                        // with some modifications.
                        int edge = -1;
                        APPBARDATA _abd;
                        SecureZeroMemory(&_abd, sizeof(_abd));
                        _abd.cbSize = sizeof(_abd);
                        _abd.hWnd = m_lpFindWindowW(L"Shell_TrayWnd", nullptr);
                        if (_abd.hWnd) {
                            const HMONITOR windowMonitor =
                                m_lpMonitorFromWindow(msg->hwnd,
                                                      MONITOR_DEFAULTTONEAREST);
                            const HMONITOR taskbarMonitor =
                                m_lpMonitorFromWindow(_abd.hWnd,
                                                      MONITOR_DEFAULTTOPRIMARY);
                            if (taskbarMonitor == windowMonitor) {
                                m_lpSHAppBarMessage(ABM_GETTASKBARPOS, &_abd);
                                edge = _abd.uEdge;
                            }
                        }
                        top = edge == ABE_TOP;
                        bottom = edge == ABE_BOTTOM;
                        left = edge == ABE_LEFT;
                        right = edge == ABE_RIGHT;
                    }
                    // If there's a taskbar on any side of the monitor, reduce
                    // our size a little bit on that edge.
                    // Note to future code archeologists:
                    // This doesn't seem to work for fullscreen on the primary
                    // display. However, testing a bunch of other apps with
                    // fullscreen modes and an auto-hiding taskbar has
                    // shown that _none_ of them reveal the taskbar from
                    // fullscreen mode. This includes Edge, Firefox, Chrome,
                    // Sublime Text, PowerPoint - none seemed to support this.
                    // This does however work fine for maximized.
                    if (top) {
                        // Peculiarly, when we're fullscreen,
                        clientRect->top += kAutoHideTaskbarThicknessPy;
                    } else if (bottom) {
                        clientRect->bottom -= kAutoHideTaskbarThicknessPy;
                    } else if (left) {
                        clientRect->left += kAutoHideTaskbarThicknessPx;
                    } else if (right) {
                        clientRect->right -= kAutoHideTaskbarThicknessPx;
                    }
                }
                // We cannot return WVR_REDRAW when there is nonclient area, or
                // Windows exhibits bugs where client pixels and child HWNDs are
                // mispositioned by the width/height of the upper-left nonclient
                // area.
                *result = 0;
            }
            return true;
        }
        case WM_NCUAHDRAWCAPTION:
        case WM_NCUAHDRAWFRAME: {
            // These undocumented messages are sent to draw themed window
            // borders. Block them to prevent drawing borders over the client
            // area.
            *result = 0;
            return true;
        }
        case WM_NCPAINT: {
            // 边框阴影处于非客户区的范围，因此如果直接阻止非客户区的绘制，会导致边框阴影丢失

            if (IsDwmCompositionEnabled()) {
                break;
            } else {
                // Only block WM_NCPAINT when DWM composition is disabled. If
                // it's blocked when DWM composition is enabled, the frame
                // shadow won't be drawn.
                *result = 0;
                return true;
            }
        }
        case WM_NCACTIVATE: {
            // DefWindowProc won't repaint the window border if lParam (normally
            // a HRGN) is -1. See the following link's "lParam" section:
            // https://docs.microsoft.com/en-us/windows/win32/winmsg/wm-ncactivate
            // Don't use "*result = 0" otherwise the window won't respond to
            // the window active state change.
            *result =
                m_lpDefWindowProcW(msg->hwnd, msg->message, msg->wParam, -1);
            return true;
        }
        case WM_NCHITTEST: {
            // 原生Win32窗口只有顶边是在窗口内部resize的，其余三边都是在窗口
            // 外部进行resize的，其原理是，WS_THICKFRAME这个窗口样式会在窗
            // 口的左、右和底边添加三个透明的resize区域，这三个区域在正常状态
            // 下是完全不可见的，它们由DWM负责绘制和控制。这些区域的宽度等于
            // (SM_CXSIZEFRAME + SM_CXPADDEDBORDER)，高度等于
            // (SM_CYSIZEFRAME + SM_CXPADDEDBORDER)，在100%缩放时，均等
            // 于8像素。它们属于窗口区域的一部分，但不属于客户区，而是属于非客
            // 户区，因此GetWindowRect获取的区域中是包含这三个resize区域的，
            // 而GetClientRect获取的区域是不包含它们的。当把
            // DWMWA_EXTENDED_FRAME_BOUNDS作为参数调用
            // DwmGetWindowAttribute时，也能获取到一个窗口大小，这个大小介
            // 于前面两者之间，暂时不知道这个数据的意义及其作用。我们在
            // WM_NCCALCSIZE消息的处理中，已经把整个窗口都设置为客户区了，也
            // 就是说，我们的窗口已经没有非客户区了，因此那三个透明的resize区
            // 域，此刻也已经成为窗口客户区的一部分了，从而变得不透明了。所以
            // 现在的resize，看起来像是在窗口内部resize，是因为原本透明的地方
            // 现在变得不透明了，实际上，单纯从范围上来看，现在我们resize的地方，
            // 就是普通窗口的边框外部，那三个透明区域的范围。
            // 因此，如果我们把边框完全去掉（就是我们正在做的事情），resize就
            // 会看起来是在内部进行，这个问题通过常规方法非常难以解决。我测试过
            // QQ和钉钉的窗口，它们的窗口就是在外部resize，但实际上它们是通过
            // 把窗口实际的内容，嵌入到一个完全透明的但尺寸要大一圈的窗口中实现
            // 的，虽然看起来效果还行，但在我看来不是正途。而且我之所以能发现，
            // 也是由于这种方法在很多情况下会露馅，比如窗口未响应卡住或贴边的时
            // 候，能明显看到窗口周围多出来一圈边界。我曾经尝试再把那三个区域弄
            // 透明，但无一例外都会破坏DWM绘制的边框阴影，因此只好作罢。

            // As you may have found, if you use this code, the resize areas
            // will be inside the frameless window, however, a normal Win32
            // window can be resized outside of it. Here is the reason: the
            // WS_THICKFRAME window style will cause a window has three
            // transparent areas beside the window's left, right and bottom
            // edge. Their width or height is eight pixels if the window is not
            // scaled. In most cases, they are totally invisible. It's DWM's
            // responsibility to draw and control them. They exist to let the
            // user resize the window, visually outside of it. They are in the
            // window area, but not the client area, so they are in the
            // non-client area actually. But we have turned the whole window
            // area into client area in WM_NCCALCSIZE, so the three transparent
            // resize areas also become a part of the client area and thus they
            // become visible. When we resize the window, it looks like we are
            // resizing inside of it, however, that's because the transparent
            // resize areas are visible now, we ARE resizing outside of the
            // window actually. But I don't know how to make them become
            // transparent again without breaking the frame shadow drawn by DWM.
            // If you really want to solve it, you can try to embed your window
            // into a larger transparent window and draw the frame shadow
            // yourself. As what we have said in WM_NCCALCSIZE, you can only
            // remove the top area of the window, this will let us be able to
            // resize outside of the window and don't need much process in this
            // message, it looks like a perfect plan, however, the top border is
            // missing due to the whole top area is removed, and it's very hard
            // to bring it back because we have to use a trick in WM_PAINT
            // (learned from Windows Terminal), but no matter what we do in
            // WM_PAINT, it will always break the backing store mechanism of Qt,
            // so actually we can't do it. And it's very difficult to do such
            // things in NativeEventFilters as well. What's worse, if we really
            // do this, the four window borders will become white and they look
            // horrible in dark mode. This solution only supports Windows 10
            // because the border width on Win10 is only one pixel, however it's
            // eight pixels on Windows 7 so preserving the three window borders
            // looks terrible on old systems. I'm testing this solution in
            // another branch, if you are interested in it, you can give it a
            // try.
            if (data->windowData.mouseTransparent) {
                // Mouse events will be passed to the parent window.
                *result = HTTRANSPARENT;
                return true;
            }
            const auto getHTResult = [](HWND _hWnd, LPARAM _lParam,
                                        const WINDOW *_data) -> LRESULT {
                const auto isInSpecificAreas = [](const int x, const int y,
                                                  const QVector<QRect> &areas,
                                                  const qreal dpr) -> bool {
                    if (!areas.isEmpty()) {
                        for (auto &&area : qAsConst(areas)) {
                            if (!area.isValid()) {
                                continue;
                            }
                            if (QRect(qRound(area.x() * dpr),
                                      qRound(area.y() * dpr),
                                      qRound(area.width() * dpr),
                                      qRound(area.height() * dpr))
                                    .contains(x, y)) {
                                return true;
                            }
                        }
                    }
                    return false;
                };
#if defined(QT_WIDGETS_LIB) || defined(QT_QUICK_LIB)
                const auto isInSpecificObjects =
                    [](const int x, const int y,
                       const QVector<QPointer<QObject>> &objects,
                       const qreal dpr) -> bool {
                    if (!objects.isEmpty()) {
                        for (auto &&object : qAsConst(objects)) {
                            if (!object) {
                                continue;
                            }
#ifdef QT_WIDGETS_LIB
                            const auto widget = qobject_cast<QWidget *>(object);
                            if (widget) {
                                if (QRect(qRound(widget->x() * dpr),
                                          qRound(widget->y() * dpr),
                                          qRound(widget->width() * dpr),
                                          qRound(widget->height() * dpr))
                                        .contains(x, y)) {
                                    return true;
                                }
                            }
#endif
#ifdef QT_QUICK_LIB
                            const auto quickItem =
                                qobject_cast<QQuickItem *>(object);
                            if (quickItem) {
                                if (QRect(qRound(quickItem->x() * dpr),
                                          qRound(quickItem->y() * dpr),
                                          qRound(quickItem->width() * dpr),
                                          qRound(quickItem->height() * dpr))
                                        .contains(x, y)) {
                                    return true;
                                }
                            }
#endif
                        }
                    }
                    return false;
                };
#endif
                RECT clientRect = {0, 0, 0, 0};
                m_lpGetClientRect(_hWnd, &clientRect);
                const LONG ww = clientRect.right;
                const LONG wh = clientRect.bottom;
                POINT mouse;
                // Don't use HIWORD(lParam) and LOWORD(lParam) to get cursor
                // coordinates because their results are unsigned numbers,
                // however the cursor position may be negative due to in a
                // different monitor.
                mouse.x = GET_X_LPARAM(_lParam);
                mouse.y = GET_Y_LPARAM(_lParam);
                m_lpScreenToClient(_hWnd, &mouse);
                const RECT frame = GetFrameSizeForWindow(_hWnd, true);
                // These values are DPI-aware.
                const LONG bw = frame.left; // identical to right
                // identical to top, if the latter doesn't include the title bar
                // height
                const LONG bh = frame.bottom;
                const LONG tbh = frame.top;
                const qreal dpr = GetDevicePixelRatioForWindow(_hWnd);
                const bool isInIgnoreAreas = isInSpecificAreas(
                    mouse.x, mouse.y, _data->windowData.ignoreAreas, dpr);
                const bool isInDraggableAreas =
                    _data->windowData.draggableAreas.isEmpty()
                    ? true
                    : isInSpecificAreas(mouse.x, mouse.y,
                                        _data->windowData.draggableAreas, dpr);
#if defined(QT_WIDGETS_LIB) || defined(QT_QUICK_LIB)
                const bool isInIgnoreObjects = isInSpecificObjects(
                    mouse.x, mouse.y, _data->windowData.ignoreObjects, dpr);
                const bool isInDraggableObjects =
                    _data->windowData.draggableObjects.isEmpty()
                    ? true
                    : isInSpecificObjects(mouse.x, mouse.y,
                                          _data->windowData.draggableObjects,
                                          dpr);
#else
                // Don't block resizing if both of the Qt Widgets module and Qt
                // Quick module are not compiled in, although there's not much
                // significance of using this code in this case.
                const bool isInIgnoreObjects = false;
                const bool isInDraggableObjects = true;
#endif
                const bool isResizePermitted = !isInIgnoreAreas &&
                    isInDraggableAreas && !isInIgnoreObjects &&
                    isInDraggableObjects;
                const bool isTitlebar = (mouse.y <= tbh) && isResizePermitted;
                if (IsMaximized(_hWnd)) {
                    if (isTitlebar) {
                        return HTCAPTION;
                    }
                    return HTCLIENT;
                }
                const bool isTop = (mouse.y <= bh) && isResizePermitted;
                const bool isBottom =
                    (mouse.y >= (wh - bh)) && isResizePermitted;
                // Make the border a little wider to let the user easy to resize
                // on corners.
                const int factor = (isTop || isBottom) ? 2 : 1;
                const bool isLeft =
                    (mouse.x <= (bw * factor)) && isResizePermitted;
                const bool isRight =
                    (mouse.x >= (ww - (bw * factor))) && isResizePermitted;
                const bool fixedSize = _data->windowData.fixedSize;
                const auto getBorderValue = [fixedSize](int value) -> int {
                    // HTBORDER: non-resizeable window border.
                    return fixedSize ? HTBORDER : value;
                };
                if (isTop) {
                    if (isLeft) {
                        return getBorderValue(HTTOPLEFT);
                    }
                    if (isRight) {
                        return getBorderValue(HTTOPRIGHT);
                    }
                    return getBorderValue(HTTOP);
                }
                if (isBottom) {
                    if (isLeft) {
                        return getBorderValue(HTBOTTOMLEFT);
                    }
                    if (isRight) {
                        return getBorderValue(HTBOTTOMRIGHT);
                    }
                    return getBorderValue(HTBOTTOM);
                }
                if (isLeft) {
                    return getBorderValue(HTLEFT);
                }
                if (isRight) {
                    return getBorderValue(HTRIGHT);
                }
                if (isTitlebar) {
                    return HTCAPTION;
                }
                return HTCLIENT;
            };
            *result = getHTResult(msg->hwnd, msg->lParam, data);
            return true;
        }
        case WM_GETMINMAXINFO: {
            // We can set the maximum and minimum size of the window in this
            // message.
            const MONITORINFO monitorInfo = GetMonitorInfoForWindow(msg->hwnd);
            const RECT rcWorkArea = monitorInfo.rcWork;
            const RECT rcMonitorArea = monitorInfo.rcMonitor;
            auto &mmi = *reinterpret_cast<LPMINMAXINFO>(msg->lParam);
            if (QOperatingSystemVersion::current() <
                QOperatingSystemVersion::Windows8) {
                // FIXME: Buggy on Windows 7:
                // The origin of coordinates is the top left edge of the
                // monitor's work area. Why? It should be the top left edge of
                // the monitor's area.
                mmi.ptMaxPosition.x = rcMonitorArea.left;
                mmi.ptMaxPosition.y = rcMonitorArea.top;
            } else {
                // Works fine on Windows 8/8.1/10
                mmi.ptMaxPosition.x =
                    qAbs(rcWorkArea.left - rcMonitorArea.left);
                mmi.ptMaxPosition.y = qAbs(rcWorkArea.top - rcMonitorArea.top);
            }
            if (data->windowData.maximumSize.isEmpty()) {
                mmi.ptMaxSize.x = qAbs(rcWorkArea.right - rcWorkArea.left);
                mmi.ptMaxSize.y = qAbs(rcWorkArea.bottom - rcWorkArea.top);
            } else {
                mmi.ptMaxSize.x =
                    qRound(GetDevicePixelRatioForWindow(msg->hwnd) *
                           data->windowData.maximumSize.width());
                mmi.ptMaxSize.y =
                    qRound(GetDevicePixelRatioForWindow(msg->hwnd) *
                           data->windowData.maximumSize.height());
            }
            mmi.ptMaxTrackSize.x = mmi.ptMaxSize.x;
            mmi.ptMaxTrackSize.y = mmi.ptMaxSize.y;
            if (!data->windowData.minimumSize.isEmpty()) {
                mmi.ptMinTrackSize.x =
                    qRound(GetDevicePixelRatioForWindow(msg->hwnd) *
                           data->windowData.minimumSize.width());
                mmi.ptMinTrackSize.y =
                    qRound(GetDevicePixelRatioForWindow(msg->hwnd) *
                           data->windowData.minimumSize.height());
            }
            *result = 0;
            return true;
        }
        case WM_SETICON:
        case WM_SETTEXT: {
            // Disable painting while these messages are handled to prevent them
            // from drawing a window caption over the client area.
            const auto oldStyle = m_lpGetWindowLongPtrW(msg->hwnd, GWL_STYLE);
            // Prevent Windows from drawing the default title bar by temporarily
            // toggling the WS_VISIBLE style.
            m_lpSetWindowLongPtrW(msg->hwnd, GWL_STYLE, oldStyle & ~WS_VISIBLE);
            updateWindow(msg->hwnd, true, false);
            const LRESULT ret = m_lpDefWindowProcW(msg->hwnd, msg->message,
                                                   msg->wParam, msg->lParam);
            m_lpSetWindowLongPtrW(msg->hwnd, GWL_STYLE, oldStyle);
            updateWindow(msg->hwnd, true, false);
            *result = ret;
            return true;
        }
        case WM_ACTIVATE:
        case WM_DWMCOMPOSITIONCHANGED:
            // DWM won't draw the frame shadow if the window doesn't have a
            // frame. So extend the window frame a bit to make sure we still
            // have the frame shadow. But don't worry, the extended window frame
            // won't be seen by the user because it's covered by the client area
            // as what we did in WM_NCCALCSIZE.
            UpdateFrameMarginsForWindow(msg->hwnd);
            break;
        case WM_DPICHANGED:
            // Sent when the effective dots per inch (dpi) for a window has
            // changed. You won't get this message until Windows 8.1
            // wParam: The HIWORD of the wParam contains the Y-axis value of
            // the new dpi of the window. The LOWORD of the wParam contains
            // the X-axis value of the new DPI of the window. For example,
            // 96, 120, 144, or 192. The values of the X-axis and the Y-axis
            // are identical for Windows apps.
            // lParam: A pointer to a RECT structure that provides a suggested
            // size and position of the current window scaled for the new DPI.
            // The expectation is that apps will reposition and resize windows
            // based on the suggestions provided by lParam when handling this
            // message.
            // Return value: If an application processes this message, it
            // should return zero.
            // See MSDN for more accurate and detailed information:
            // https://docs.microsoft.com/en-us/windows/win32/hidpi/wm-dpichanged
            // Note: Qt will do the scaling automatically, there is no need
            // to do this yourself. See:
            // https://code.qt.io/cgit/qt/qtbase.git/tree/src/plugins/platforms/windows/qwindowscontext.cpp
            break;
        default:
            break;
        }
    }
    return false;
}

void WinNativeEventFilter::setWindowData(HWND window, const WINDOWDATA *data) {
    ResolveWin32APIs();
    if (window && m_lpIsWindow(window) && data) {
        createUserData(window, data);
    }
}

WinNativeEventFilter::WINDOWDATA *
WinNativeEventFilter::windowData(HWND window) {
    ResolveWin32APIs();
    if (window && m_lpIsWindow(window)) {
        createUserData(window);
        return &reinterpret_cast<WINDOW *>(
                    m_lpGetWindowLongPtrW(window, GWLP_USERDATA))
                    ->windowData;
    }
    return nullptr;
}

void WinNativeEventFilter::setBorderWidth(int bw) { m_borderWidth = bw; }

void WinNativeEventFilter::setBorderHeight(int bh) { m_borderHeight = bh; }

void WinNativeEventFilter::setTitlebarHeight(int tbh) {
    m_titlebarHeight = tbh;
}

void WinNativeEventFilter::updateWindow(HWND handle, bool triggerFrameChange,
                                        bool redraw) {
    ResolveWin32APIs();
    if (handle && m_lpIsWindow(handle)) {
        if (triggerFrameChange) {
            m_lpSetWindowPos(handle, nullptr, 0, 0, 0, 0,
                             SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOSIZE |
                                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOOWNERZORDER);
        }
        if (redraw) {
            m_lpRedrawWindow(handle, nullptr, nullptr,
                             RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOCHILDREN);
        }
    }
}

int WinNativeEventFilter::getSystemMetric(HWND handle, SystemMetric metric,
                                          bool dpiAware) {
    ResolveWin32APIs();
    const qreal dpr = dpiAware ? GetDevicePixelRatioForWindow(handle)
                               : m_defaultDevicePixelRatio;
    if (handle && m_lpIsWindow(handle)) {
        createUserData(handle);
        const auto userData = reinterpret_cast<WINDOW *>(
            m_lpGetWindowLongPtrW(handle, GWLP_USERDATA));
        switch (metric) {
        case SystemMetric::BorderWidth: {
            const int bw = userData->windowData.borderWidth;
            if (bw > 0) {
                return qRound(bw * dpr);
            } else {
                const int result = m_lpGetSystemMetrics(SM_CXSIZEFRAME) +
                    m_lpGetSystemMetrics(SM_CXPADDEDBORDER);
                const int result_dpi =
                    GetSystemMetricsForWindow(handle, SM_CXSIZEFRAME) +
                    GetSystemMetricsForWindow(handle, SM_CXPADDEDBORDER);
                return dpiAware ? result_dpi : result;
            }
        }
        case SystemMetric::BorderHeight: {
            const int bh = userData->windowData.borderHeight;
            if (bh > 0) {
                return qRound(bh * dpr);
            } else {
                const int result = m_lpGetSystemMetrics(SM_CYSIZEFRAME) +
                    m_lpGetSystemMetrics(SM_CXPADDEDBORDER);
                const int result_dpi =
                    GetSystemMetricsForWindow(handle, SM_CYSIZEFRAME) +
                    GetSystemMetricsForWindow(handle, SM_CXPADDEDBORDER);
                return dpiAware ? result_dpi : result;
            }
        }
        case SystemMetric::TitleBarHeight: {
            const int tbh = userData->windowData.titlebarHeight;
            if (tbh > 0) {
                return qRound(tbh * dpr);
            } else {
                const int result = m_lpGetSystemMetrics(SM_CYSIZEFRAME) +
                    m_lpGetSystemMetrics(SM_CXPADDEDBORDER) +
                    m_lpGetSystemMetrics(SM_CYCAPTION);
                const int result_dpi =
                    GetSystemMetricsForWindow(handle, SM_CYSIZEFRAME) +
                    GetSystemMetricsForWindow(handle, SM_CXPADDEDBORDER) +
                    GetSystemMetricsForWindow(handle, SM_CYCAPTION);
                return dpiAware ? result_dpi : result;
            }
        }
        }
    }
    switch (metric) {
    case SystemMetric::BorderWidth: {
        if (m_borderWidth > 0) {
            return qRound(m_borderWidth * dpr);
        }
        break;
    }
    case SystemMetric::BorderHeight: {
        if (m_borderHeight > 0) {
            return qRound(m_borderHeight * dpr);
        }
        break;
    }
    case SystemMetric::TitleBarHeight: {
        if (m_titlebarHeight > 0) {
            return qRound(m_titlebarHeight * dpr);
        }
        break;
    }
    }
    return -1;
}

void WinNativeEventFilter::setWindowGeometry(HWND handle, const int x,
                                             const int y, const int width,
                                             const int height) {
    if (handle && m_lpIsWindow(handle) && (x > 0) && (y > 0) && (width > 0) &&
        (height > 0)) {
        const qreal dpr = GetDevicePixelRatioForWindow(handle);
        // Why not use SetWindowPos? Actually we can, but MoveWindow
        // sends the WM_WINDOWPOSCHANGING, WM_WINDOWPOSCHANGED, WM_MOVE,
        // WM_SIZE, and WM_NCCALCSIZE messages to the window.
        // SetWindowPos only sends WM_WINDOWPOSCHANGED.
        m_lpMoveWindow(handle, x, y, qRound(width * dpr), qRound(height * dpr),
                       TRUE);
    }
}

void WinNativeEventFilter::moveWindowToDesktopCenter(HWND handle) {
    if (handle && m_lpIsWindow(handle)) {
        const WINDOWINFO windowInfo = GetInfoForWindow(handle);
        const MONITORINFO monitorInfo = GetMonitorInfoForWindow(handle);
        // If we want to move a window to the center of the desktop,
        // I think we should use rcMonitor, the monitor's whole area,
        // to calculate the new coordinates of our window, not rcWork.
        const LONG mw =
            qAbs(monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left);
        const LONG mh =
            qAbs(monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top);
        const LONG ww =
            qAbs(windowInfo.rcWindow.right - windowInfo.rcWindow.left);
        const LONG wh =
            qAbs(windowInfo.rcWindow.bottom - windowInfo.rcWindow.top);
        m_lpMoveWindow(handle, qRound((mw - ww) / 2.0), qRound((mh - wh) / 2.0),
                       ww, wh, TRUE);
    }
}
