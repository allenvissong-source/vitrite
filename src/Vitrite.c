/*
Copyright 2002 Ryan VanMiddlesworth

This file is part of Vitrite.

Vitrite is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

Vitrite is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Vitrite; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

// This app requires Windows 2000 or newer
#include "resource.h"
#include <windows.h>
#include <shellapi.h>
#include <tchar.h>
#include <strsafe.h>
#include <stdio.h>
#include <stdlib.h>
#include "vitrite.h"

// Globals
HINSTANCE  g_hInstance;
BOOL  g_bWindowShown;
HANDLE  ghMutex;

#define TIMER_OPACITY_ANIM 1
#define OPACITY_ANIM_INTERVAL_MS 15
#define OPACITY_ANIM_STEPS 12

static BOOL g_hotkeyAlphaRegistered[10];
static BOOL g_hotkeyTopmostRegistered;
static BOOL g_hotkeyTopmostNumpadRegistered;
static UINT g_uTaskbarRestart; // Message ID for "TaskbarCreated"

// NOTIFYICON_VERSION_4 handling
#ifndef NIN_SELECT
#define NIN_SELECT (WM_USER + 0)
#endif
#ifndef NIN_KEYSELECT
#define NIN_KEYSELECT (WM_USER + 1)
#endif

typedef struct WINDOWSTATE {
	HWND hwnd;
	BOOL hadLayered;
	BOOL captured;
	COLORREF prevColorKey;
	BYTE prevAlpha;
	DWORD prevFlags;
} WINDOWSTATE;

#define WINDOWSTATE_MAX 64
static WINDOWSTATE g_windowStates[WINDOWSTATE_MAX];
static int g_windowStateEvictIndex;

static BOOL g_animActive;
static HWND g_animOwnerWnd;
static HWND g_animTargetWnd;
static BYTE g_animFromAlpha;
static BYTE g_animToAlpha;
static int g_animStep;
static int g_animSteps;
static BOOL g_animClearStateAtEnd;
static int g_animEndAction; // 0=none, 1=remove-layered, 2=restore-prev
static WINDOWSTATE g_animRestore;

static void LoadResString(UINT id, LPTSTR buf, int cchBuf, LPCTSTR fallback) {
	int n;

	if (buf == NULL || cchBuf <= 0) {
		return;
	}
	buf[0] = TEXT('\0');

	n = LoadString(g_hInstance, id, buf, cchBuf);
	if (n <= 0 && fallback != NULL) {
		StringCchCopy(buf, cchBuf, fallback);
	}
}

static void InitUiLanguage(void) {
	HMODULE hKernel32;
	LANGID lang;
	typedef LANGID (WINAPI *PFN_SetThreadUILanguage)(LANGID);
	PFN_SetThreadUILanguage pSetThreadUILanguage;

	hKernel32 = GetModuleHandle(TEXT("kernel32.dll"));
	if (hKernel32 == NULL) {
		return;
	}

	// Best-effort: set thread UI language to the user's default UI language so the
	// resource loader can pick the correct language (e.g., zh-CN vs en-US).
	lang = GetUserDefaultUILanguage();
	pSetThreadUILanguage = (PFN_SetThreadUILanguage)GetProcAddress(hKernel32, "SetThreadUILanguage");
	if (pSetThreadUILanguage != NULL) {
		pSetThreadUILanguage(lang);
	}
}

static void StopOpacityAnimation(void) {
	if (g_animActive && g_animOwnerWnd != NULL) {
		KillTimer(g_animOwnerWnd, TIMER_OPACITY_ANIM);
	}
	g_animActive = FALSE;
	g_animOwnerWnd = NULL;
	g_animTargetWnd = NULL;
	g_animStep = 0;
	g_animSteps = 0;
	g_animClearStateAtEnd = FALSE;
	g_animEndAction = 0;
	memset(&g_animRestore, 0, sizeof(g_animRestore));
}

static void RemoveWindowState(HWND hWnd) {
	int i;
	for (i = 0; i < WINDOWSTATE_MAX; i++) {
		if (g_windowStates[i].hwnd == hWnd) {
			g_windowStates[i].hwnd = NULL;
			return;
		}
	}
}

static BYTE GetCurrentWindowAlpha(HWND hWnd) {
	COLORREF key;
	BYTE alpha;
	DWORD flags;

	key = 0;
	alpha = 255;
	flags = 0;
	if (GetLayeredWindowAttributes(hWnd, &key, &alpha, &flags) != 0) {
		if (flags & LWA_ALPHA) {
			return alpha;
		}
	}
	return 255;
}

static void TickOpacityAnimation(void) {
	LONG_PTR exStyle;
	BYTE alpha;
	int alphaInt;
	int step;
	int steps;

	if (!g_animActive || g_animOwnerWnd == NULL || g_animTargetWnd == NULL) {
		StopOpacityAnimation();
		return;
	}
	if (!IsWindow(g_animTargetWnd)) {
		if (g_animClearStateAtEnd) {
			RemoveWindowState(g_animTargetWnd);
		}
		StopOpacityAnimation();
		return;
	}

	step = g_animStep;
	steps = g_animSteps;
	if (steps <= 0) {
		steps = 1;
	}
	if (step > steps) {
		step = steps;
	}

	alphaInt = (int)g_animFromAlpha + ((int)g_animToAlpha - (int)g_animFromAlpha) * step / steps;
	if (alphaInt < 0) {
		alphaInt = 0;
	} else if (alphaInt > 255) {
		alphaInt = 255;
	}
	alpha = (BYTE)alphaInt;
	exStyle = GetWindowLongPtr(g_animTargetWnd, GWL_EXSTYLE);
	SetWindowLongPtr(g_animTargetWnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
	SetLayeredWindowAttributes(g_animTargetWnd, 0, alpha, LWA_ALPHA);

	g_animStep++;
	if (g_animStep > g_animSteps) {
		if (g_animEndAction == 2) {
			exStyle = GetWindowLongPtr(g_animTargetWnd, GWL_EXSTYLE);
			SetWindowLongPtr(g_animTargetWnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
			if (g_animRestore.captured) {
				SetLayeredWindowAttributes(g_animTargetWnd,
					g_animRestore.prevColorKey, g_animRestore.prevAlpha, g_animRestore.prevFlags);
			}
		} else if (g_animEndAction == 1) {
			exStyle = GetWindowLongPtr(g_animTargetWnd, GWL_EXSTYLE);
			SetWindowLongPtr(g_animTargetWnd, GWL_EXSTYLE, exStyle & ~WS_EX_LAYERED);
			RedrawWindow(g_animTargetWnd, NULL, NULL, RDW_ERASE | RDW_INVALIDATE |
				RDW_FRAME | RDW_ALLCHILDREN);
		}

		if (g_animClearStateAtEnd) {
			RemoveWindowState(g_animTargetWnd);
		}
		StopOpacityAnimation();
	}
}

static void PruneWindowStates(void) {
	int i;
	for (i = 0; i < WINDOWSTATE_MAX; i++) {
		if (g_windowStates[i].hwnd != NULL && !IsWindow(g_windowStates[i].hwnd)) {
			g_windowStates[i].hwnd = NULL;
		}
	}
}

static WINDOWSTATE* FindWindowState(HWND hWnd) {
	int i;
	for (i = 0; i < WINDOWSTATE_MAX; i++) {
		if (g_windowStates[i].hwnd == hWnd) {
			return &g_windowStates[i];
		}
	}
	return NULL;
}

static WINDOWSTATE* GetOrCaptureWindowState(HWND hWnd) {
	WINDOWSTATE* state;
	int i;
	LONG_PTR exStyle;
	COLORREF key;
	BYTE alpha;
	DWORD flags;

	state = FindWindowState(hWnd);
	if (state != NULL) {
		return state;
	}

	for (i = 0; i < WINDOWSTATE_MAX; i++) {
		if (g_windowStates[i].hwnd == NULL) {
			state = &g_windowStates[i];
			break;
		}
	}
	if (state == NULL) {
		if (g_windowStateEvictIndex < 0 || g_windowStateEvictIndex >= WINDOWSTATE_MAX) {
			g_windowStateEvictIndex = 0;
		}
		state = &g_windowStates[g_windowStateEvictIndex++];
		if (g_windowStateEvictIndex >= WINDOWSTATE_MAX) {
			g_windowStateEvictIndex = 0;
		}
	}

	memset(state, 0, sizeof(*state));
	state->hwnd = hWnd;

	exStyle = GetWindowLongPtr(hWnd, GWL_EXSTYLE);
	state->hadLayered = ((exStyle & WS_EX_LAYERED) != 0);
	state->captured = FALSE;
	if (state->hadLayered) {
		key = 0;
		alpha = 255;
		flags = 0;
		if (GetLayeredWindowAttributes(hWnd, &key, &alpha, &flags) != 0) {
			state->prevColorKey = key;
			state->prevAlpha = alpha;
			state->prevFlags = flags;
			state->captured = TRUE;
		}
	}
	return state;
}

static void BeginOpacityAnimation(HWND hOwnerWnd, HWND hTargetWnd, BYTE toAlpha) {
	LONG_PTR exStyle;

	if (hOwnerWnd == NULL || hTargetWnd == NULL) {
		return;
	}
	StopOpacityAnimation();

	exStyle = GetWindowLongPtr(hTargetWnd, GWL_EXSTYLE);
	SetWindowLongPtr(hTargetWnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);

	g_animActive = TRUE;
	g_animOwnerWnd = hOwnerWnd;
	g_animTargetWnd = hTargetWnd;
	g_animFromAlpha = GetCurrentWindowAlpha(hTargetWnd);
	g_animToAlpha = toAlpha;
	g_animStep = 0;
	g_animSteps = OPACITY_ANIM_STEPS;
	g_animClearStateAtEnd = FALSE;
	g_animEndAction = 0;
	memset(&g_animRestore, 0, sizeof(g_animRestore));
	SetTimer(hOwnerWnd, TIMER_OPACITY_ANIM, OPACITY_ANIM_INTERVAL_MS, NULL);
}

static void BeginResetAnimation(HWND hOwnerWnd, HWND hTargetWnd) {
	WINDOWSTATE* state;
	LONG_PTR exStyle;

	if (hOwnerWnd == NULL || hTargetWnd == NULL) {
		return;
	}

	state = FindWindowState(hTargetWnd);
	if (state == NULL) {
		return;
	}

	StopOpacityAnimation();

	// If we modified a window that was already layered, restore its original attributes.
	if (state->hadLayered && state->captured) {
		// If the original state didn't use alpha, restore immediately to avoid changing flags mid-animation.
		if ((state->prevFlags & LWA_ALPHA) == 0) {
			exStyle = GetWindowLongPtr(hTargetWnd, GWL_EXSTYLE);
			SetWindowLongPtr(hTargetWnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
			SetLayeredWindowAttributes(hTargetWnd, state->prevColorKey, state->prevAlpha, state->prevFlags);
			RemoveWindowState(hTargetWnd);
			return;
		}

		g_animEndAction = 2;
		g_animRestore = *state;
		g_animToAlpha = state->prevAlpha;
	} else if (!state->hadLayered) {
		// We added WS_EX_LAYERED: fade back to opaque then remove WS_EX_LAYERED.
		g_animEndAction = 1;
		g_animToAlpha = 255;
	} else {
		// Best-effort: fade back to opaque and keep WS_EX_LAYERED (we couldn't capture the original state).
		g_animEndAction = 0;
		g_animToAlpha = 255;
	}

	exStyle = GetWindowLongPtr(hTargetWnd, GWL_EXSTYLE);
	SetWindowLongPtr(hTargetWnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);

	g_animActive = TRUE;
	g_animOwnerWnd = hOwnerWnd;
	g_animTargetWnd = hTargetWnd;
	g_animFromAlpha = GetCurrentWindowAlpha(hTargetWnd);
	g_animStep = 0;
	g_animSteps = OPACITY_ANIM_STEPS;
	g_animClearStateAtEnd = TRUE;
	SetTimer(hOwnerWnd, TIMER_OPACITY_ANIM, OPACITY_ANIM_INTERVAL_MS, NULL);
}

static void TryEnableDarkModeTitleBar(HWND hWnd) {
	typedef HRESULT (WINAPI *PFN_DwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);
	HMODULE hDwm;
	PFN_DwmSetWindowAttribute pDwmSetWindowAttribute;
	BOOL useDark;

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1
#define DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1 19
#endif
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

	hDwm = LoadLibrary(TEXT("dwmapi.dll"));
	if (hDwm == NULL) {
		return;
	}
	pDwmSetWindowAttribute = (PFN_DwmSetWindowAttribute)GetProcAddress(hDwm, "DwmSetWindowAttribute");
	if (pDwmSetWindowAttribute == NULL) {
		FreeLibrary(hDwm);
		return;
	}

	useDark = TRUE;
	// Best-effort: try both attribute IDs used by different Windows 10/11 builds.
	pDwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(useDark));
	pDwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1, &useDark, sizeof(useDark));
	FreeLibrary(hDwm);
}

static BOOL IsBlacklistedWindowClass(HWND hWnd) {
	TCHAR className[128];
	if (GetClassName(hWnd, className, (int)(sizeof(className) / sizeof(className[0]))) == 0) {
		return FALSE;
	}
	// Avoid special/system UI windows where changing transparency is undesirable.
	return (lstrcmpi(className, TEXT("Shell_TrayWnd")) == 0) ||
		(lstrcmpi(className, TEXT("Shell_SecondaryTrayWnd")) == 0) ||
		(lstrcmpi(className, TEXT("Progman")) == 0) ||
		(lstrcmpi(className, TEXT("WorkerW")) == 0);
}

static void ApplyTransparencyTenths(HWND hOurWnd, int iTenths) {
	HWND hActiveWindow;
	WINDOWSTATE* state;
	BYTE targetAlpha;

	if (iTenths < 0 || iTenths > 9) {
		return;
	}

	PruneWindowStates();

	hActiveWindow = GetForegroundWindow();
	if (hActiveWindow == NULL || hActiveWindow == hOurWnd) {
		return;
	}
	if (!IsWindowVisible(hActiveWindow) || IsBlacklistedWindowClass(hActiveWindow)) {
		return;
	}

	if (iTenths > 0) {
		state = GetOrCaptureWindowState(hActiveWindow);
		if (state == NULL) {
			return;
		}

		targetAlpha = (iTenths == 9) ? 255 : (BYTE)((iTenths * 255) / 10);
		BeginOpacityAnimation(hOurWnd, hActiveWindow, targetAlpha);
	} else {
		BeginResetAnimation(hOurWnd, hActiveWindow);
	}
}

static void ToggleTopmost(HWND hOurWnd) {
	HWND hActiveWindow;
	LONG_PTR exStyle;

	hActiveWindow = GetForegroundWindow();
	if (hActiveWindow == NULL || hActiveWindow == hOurWnd) {
		return;
	}
	if (!IsWindowVisible(hActiveWindow) || IsBlacklistedWindowClass(hActiveWindow)) {
		return;
	}

	exStyle = GetWindowLongPtr(hActiveWindow, GWL_EXSTYLE);
	if (exStyle & WS_EX_TOPMOST) {
		SetWindowPos(hActiveWindow, HWND_NOTOPMOST, 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE);
	} else {
		SetWindowPos(hActiveWindow, HWND_TOPMOST, 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE);
	}
}

BOOL AddIconToSystemTray(HWND hWnd, UINT uID, LPCTSTR lpszTip) {
	NOTIFYICONDATA  tnid;
	int cx, cy;
	int cchTip;

	memset(&tnid, 0, sizeof(tnid));
	tnid.cbSize = sizeof(tnid);
	tnid.hWnd = hWnd;
	tnid.uID = uID;
	tnid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
	tnid.uCallbackMessage = TRAY_CALLBACK;

	cx = GetSystemMetrics(SM_CXSMICON);
	cy = GetSystemMetrics(SM_CYSMICON);
	tnid.hIcon = (HICON) LoadImage(g_hInstance, MAKEINTRESOURCE(IDI_ICON1),
		IMAGE_ICON, cx, cy, LR_SHARED);

	cchTip = (int)(sizeof(tnid.szTip) / sizeof(tnid.szTip[0]));
	if (lpszTip)
		lstrcpyn(tnid.szTip, lpszTip, cchTip);
	else 
		tnid.szTip[0] = '\0'; 

	if (!Shell_NotifyIcon(NIM_ADD, &tnid)) {
		return FALSE;
	}

	tnid.uVersion = NOTIFYICON_VERSION_4;
	Shell_NotifyIcon(NIM_SETVERSION, &tnid);
	return TRUE;
}

BOOL RemoveIconFromSystemTray(HWND hWnd, UINT uID) { 
	NOTIFYICONDATA tnid; 

	memset(&tnid, 0, sizeof(tnid));
	tnid.cbSize = sizeof(tnid); 
	tnid.hWnd = hWnd; 
	tnid.uID = uID; 
	return(Shell_NotifyIcon(NIM_DELETE, &tnid)); 
} 

BOOL ShowPopupMenu(HWND hWnd, POINT pOint) {
	HMENU  hPopup;
	TCHAR titleError[64];
	TCHAR msgCreatePopup[128];
	TCHAR msgTrackPopup[128];
	TCHAR menuAbout[64];
	TCHAR menuExit[64];

	//	Create popup menu on the fly. We'll use InsertMenu or AppendMenu 
	//  to add items to it.
	hPopup = CreatePopupMenu();
	if (!hPopup) {
		LoadResString(IDS_TITLE_ERROR, titleError, (int)(sizeof(titleError) / sizeof(titleError[0])), TEXT("Error"));
		LoadResString(IDS_ERR_CREATEPOPUP, msgCreatePopup, (int)(sizeof(msgCreatePopup) / sizeof(msgCreatePopup[0])),
			TEXT("CreatePopupMenu failed"));
		MessageBox(NULL, msgCreatePopup, titleError, MB_OK);
		return FALSE;
	}

	LoadResString(IDS_MENU_ABOUT, menuAbout, (int)(sizeof(menuAbout) / sizeof(menuAbout[0])), TEXT("&About"));
	LoadResString(IDS_MENU_EXIT, menuExit, (int)(sizeof(menuExit) / sizeof(menuExit[0])), TEXT("E&xit"));
	AppendMenu(hPopup,
		MF_STRING | ((g_bWindowShown)? MF_GRAYED : 0),
		IDM_MMAIN,
		menuAbout);
	AppendMenu(hPopup, MF_STRING, IDM_MEXIT, menuExit);

	// We have to do some MS voodoo to make the taskbar 
	// notification menus work right. It's not my fault - 
	// Microsoft is to blame on this one.
	// See MS Knowledgebase article Q135788 for more info.
	SetForegroundWindow(hWnd);		// [MS voodoo]
	if (!TrackPopupMenuEx(hPopup, TPM_RIGHTALIGN | TPM_BOTTOMALIGN,
		pOint.x, pOint.y, hWnd, NULL)) {
		LoadResString(IDS_TITLE_ERROR, titleError, (int)(sizeof(titleError) / sizeof(titleError[0])), TEXT("Error"));
		LoadResString(IDS_ERR_TRACKPOPUP, msgTrackPopup, (int)(sizeof(msgTrackPopup) / sizeof(msgTrackPopup[0])),
			TEXT("TrackPopupMenu failed"));
		MessageBox(NULL, msgTrackPopup, titleError, MB_OK);
		DestroyMenu(hPopup);
		return FALSE;
	}
	PostMessage(hWnd, WM_NULL, 0, 0);	// [More MS voodoo]
	DestroyMenu(hPopup);

	return TRUE;
}

INT_PTR CALLBACK MainDlgProc(HWND hDlg,
							 UINT Msg,
							 WPARAM wParam,
							 LPARAM lParam) {
	RECT  DlgRect;
	POINT  pMenuPoint;
	int i;

	switch(Msg) {
	case WM_INITDIALOG :
		GetWindowRect(hDlg, &DlgRect);
		SetWindowPos(hDlg, HWND_TOP,
			(GetSystemMetrics(SM_CXSCREEN)-(DlgRect.right-DlgRect.left))/2, 
			(GetSystemMetrics(SM_CYSCREEN)-(DlgRect.bottom-DlgRect.top))/2,
			0, 0, SWP_NOSIZE);
		TryEnableDarkModeTitleBar(hDlg);
		return(TRUE);

	case WM_DESTROY :
		StopOpacityAnimation();
		// Unregister hotkeys
		for (i = 0; i <= 9; i++) {
			if (g_hotkeyAlphaRegistered[i]) {
				UnregisterHotKey(hDlg, HOTKEY_ALPHA_0 + i);
			}
		}
		if (g_hotkeyTopmostRegistered) {
			UnregisterHotKey(hDlg, HOTKEY_TOPMOST);
		}
		if (g_hotkeyTopmostNumpadRegistered) {
			UnregisterHotKey(hDlg, HOTKEY_TOPMOST_NUMPAD);
		}
		RemoveIconFromSystemTray(hDlg, IDI_ICON1);
		if (ghMutex != NULL) {
			CloseHandle(ghMutex);
			ghMutex = NULL;
		}
		PostQuitMessage(0);
		return(TRUE);

	case TRAY_CALLBACK :
		switch(lParam) {
		case NIN_SELECT :
		case WM_CONTEXTMENU :
		case WM_RBUTTONUP :
		case WM_LBUTTONUP :
			GetCursorPos(&pMenuPoint);
			ShowPopupMenu(hDlg, pMenuPoint);
			return(TRUE);

		case NIN_KEYSELECT :
		case WM_LBUTTONDBLCLK :
			ShowWindow(hDlg, SW_SHOW);
			UpdateWindow(hDlg);
			g_bWindowShown = TRUE;
			return(TRUE);
		}
		return(TRUE);

	default:
		if (Msg == g_uTaskbarRestart && g_uTaskbarRestart != 0) {
			// Explorer crashed or restarted - re-add our icon
			TCHAR cTrayTip[128];
			TCHAR trayTipFormat[64];
			LoadResString(IDS_TRAYTIP_FORMAT, trayTipFormat, (int)(sizeof(trayTipFormat) / sizeof(trayTipFormat[0])),
				TEXT("Vitrite - %s"));
			StringCchPrintf(cTrayTip, (int)(sizeof(cTrayTip) / sizeof(cTrayTip[0])),
				trayTipFormat, VITRITE_VERSION);
			AddIconToSystemTray(hDlg, IDI_ICON1, cTrayTip);
			return(TRUE);
		}
		break;

	case WM_COMMAND :
		switch(LOWORD(wParam)) {
		case IDOK :
			ShowWindow(hDlg, SW_HIDE);
			g_bWindowShown = FALSE;
			return(TRUE);

		case IDM_MEXIT :
			PostQuitMessage(0);
			return(TRUE);

		case IDM_MMAIN :
			ShowWindow(hDlg, SW_SHOW);
			UpdateWindow(hDlg);
			g_bWindowShown = TRUE;
			return(TRUE);
		}		
		return(TRUE);

	case WM_HOTKEY :
		if (wParam >= HOTKEY_ALPHA_0 && wParam <= HOTKEY_ALPHA_9) {
			ApplyTransparencyTenths(hDlg, (int)(wParam - HOTKEY_ALPHA_0));
			return(TRUE);
		} else if (wParam == HOTKEY_TOPMOST || wParam == HOTKEY_TOPMOST_NUMPAD) {
			ToggleTopmost(hDlg);
			return(TRUE);
		}
		return(TRUE);

	case WM_TIMER :
		if (wParam == TIMER_OPACITY_ANIM) {
			TickOpacityAnimation();
			return(TRUE);
		}
		return(TRUE);
	}	
	return(FALSE);
}

int APIENTRY _tWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
					   LPTSTR lpCmdLine, int nCmdShow) {
	HWND  hWnd;
	MSG  Msg;
	TCHAR  cTrayTip[128];
	int i;
	TCHAR titleError[64];
	TCHAR titleWarning[64];
	TCHAR msgMutexCreate[128];
	TCHAR msgAlreadyRunning[128];
	TCHAR msgTrayIcon[128];
	TCHAR msgNoHotkeys[256];
	TCHAR trayTipFormat[64];

	(void)hPrevInstance;
	(void)lpCmdLine;
	(void)nCmdShow;

	g_hInstance = hInstance;
	InitUiLanguage();

	g_uTaskbarRestart = RegisterWindowMessage(TEXT("TaskbarCreated"));

	// Create/check for a mutex to determine if another copy of Vitrite is already running.
	if ((ghMutex = CreateMutex(NULL, FALSE, TEXT("_vitrite_mutex"))) == NULL) {
		LoadResString(IDS_TITLE_ERROR, titleError, (int)(sizeof(titleError) / sizeof(titleError[0])), TEXT("Critical Error"));
		LoadResString(IDS_ERR_MUTEX_CREATE, msgMutexCreate, (int)(sizeof(msgMutexCreate) / sizeof(msgMutexCreate[0])),
			TEXT("Unable to create mutex"));
		MessageBox(NULL, msgMutexCreate, titleError, MB_OK | MB_ICONSTOP);
		return 1;
	} else if (GetLastError() == ERROR_ALREADY_EXISTS) {
		LoadResString(IDS_TITLE_WARNING, titleWarning, (int)(sizeof(titleWarning) / sizeof(titleWarning[0])), TEXT("Warning"));
		LoadResString(IDS_WARN_ALREADY_RUNNING, msgAlreadyRunning, (int)(sizeof(msgAlreadyRunning) / sizeof(msgAlreadyRunning[0])),
			TEXT("Another copy of Vitrite is already running."));
		MessageBox(NULL, msgAlreadyRunning, titleWarning, MB_OK | MB_ICONEXCLAMATION);
		CloseHandle(ghMutex);
		ghMutex = NULL;
		return 0;
	}

	memset(&Msg, 0, sizeof(Msg));
	// Build/show main dialog
	hWnd = CreateDialog(hInstance,
		MAKEINTRESOURCE(IDD_MAIN),
		NULL,
		MainDlgProc);
	SendMessage(hWnd, WM_SETICON, 
		(WPARAM) ICON_BIG,
		(LPARAM) (HICON) LoadImage(hInstance,
		MAKEINTRESOURCE(IDI_ICON1),
		IMAGE_ICON, 32, 32, 0));

	ShowWindow(hWnd, SW_HIDE);
	g_bWindowShown = FALSE;
	UpdateWindow(hWnd);

	LoadResString(IDS_TRAYTIP_FORMAT, trayTipFormat, (int)(sizeof(trayTipFormat) / sizeof(trayTipFormat[0])),
		TEXT("Vitrite - %s"));
	StringCchPrintf(cTrayTip, (int)(sizeof(cTrayTip) / sizeof(cTrayTip[0])),
		trayTipFormat, VITRITE_VERSION);
	if (!AddIconToSystemTray(hWnd, IDI_ICON1, cTrayTip)) {
		LoadResString(IDS_TITLE_ERROR, titleError, (int)(sizeof(titleError) / sizeof(titleError[0])), TEXT("Critical Error"));
		LoadResString(IDS_ERR_TRAYICON, msgTrayIcon, (int)(sizeof(msgTrayIcon) / sizeof(msgTrayIcon[0])),
			TEXT("Unable to put icon on taskbar."));
		MessageBox(NULL, msgTrayIcon, titleError, MB_ICONSTOP | MB_OK);
		DestroyWindow(hWnd);
		return (int)(INT_PTR)Msg.wParam;
	}

	// Register global hotkeys (no DLL injection / no global hook).
	memset(g_hotkeyAlphaRegistered, 0, sizeof(g_hotkeyAlphaRegistered));
	g_hotkeyTopmostRegistered = FALSE;
	g_hotkeyTopmostNumpadRegistered = FALSE;

	{
		TCHAR warning[512];
		TCHAR header[256];
		TCHAR alphaLineFormat[64];
		TCHAR plusMainLine[96];
		TCHAR plusNumpadLine[96];
		BOOL anyRegistered;
		TCHAR line[64];
		TCHAR message[1024];

		warning[0] = TEXT('\0');
		anyRegistered = FALSE;

		for (i = 0; i <= 9; i++) {
			if (RegisterHotKey(hWnd, HOTKEY_ALPHA_0 + i, MOD_CONTROL | MOD_SHIFT, 0x30 + i)) {
				g_hotkeyAlphaRegistered[i] = TRUE;
				anyRegistered = TRUE;
			} else {
				LoadResString(IDS_WARN_HOTKEY_ALPHA_LINE, alphaLineFormat,
					(int)(sizeof(alphaLineFormat) / sizeof(alphaLineFormat[0])), TEXT("  - Ctrl+Shift+%d\r\n"));
				StringCchPrintf(line, (int)(sizeof(line) / sizeof(line[0])),
					alphaLineFormat, i);
				StringCchCat(warning, (int)(sizeof(warning) / sizeof(warning[0])), line);
			}
		}

		if (RegisterHotKey(hWnd, HOTKEY_TOPMOST, MOD_CONTROL | MOD_SHIFT, VK_OEM_PLUS)) {
			g_hotkeyTopmostRegistered = TRUE;
			anyRegistered = TRUE;
		} else {
			LoadResString(IDS_WARN_HOTKEY_PLUS_MAIN, plusMainLine,
				(int)(sizeof(plusMainLine) / sizeof(plusMainLine[0])), TEXT("  - Ctrl+Shift+Plus (main keyboard)\r\n"));
			StringCchCat(warning, (int)(sizeof(warning) / sizeof(warning[0])), plusMainLine);
		}

		if (RegisterHotKey(hWnd, HOTKEY_TOPMOST_NUMPAD, MOD_CONTROL | MOD_SHIFT, VK_ADD)) {
			g_hotkeyTopmostNumpadRegistered = TRUE;
			anyRegistered = TRUE;
		} else {
			LoadResString(IDS_WARN_HOTKEY_PLUS_NUMPAD, plusNumpadLine,
				(int)(sizeof(plusNumpadLine) / sizeof(plusNumpadLine[0])), TEXT("  - Ctrl+Shift+Numpad+\r\n"));
			StringCchCat(warning, (int)(sizeof(warning) / sizeof(warning[0])), plusNumpadLine);
		}

		if (!anyRegistered) {
			LoadResString(IDS_TITLE_ERROR, titleError, (int)(sizeof(titleError) / sizeof(titleError[0])), TEXT("Critical Error"));
			LoadResString(IDS_ERR_NO_HOTKEYS, msgNoHotkeys, (int)(sizeof(msgNoHotkeys) / sizeof(msgNoHotkeys[0])),
				TEXT("Unable to register any hotkeys.\r\nAnother program may be using them."));
			MessageBox(NULL, msgNoHotkeys, titleError, MB_OK | MB_ICONSTOP);
			RemoveIconFromSystemTray(hWnd, IDI_ICON1);
			DestroyWindow(hWnd);
			return (int)(INT_PTR)Msg.wParam;
		}

		if (warning[0] != TEXT('\0')) {
			LoadResString(IDS_TITLE_WARNING, titleWarning, (int)(sizeof(titleWarning) / sizeof(titleWarning[0])), TEXT("Warning"));
			LoadResString(IDS_WARN_HOTKEYS_HEADER, header, (int)(sizeof(header) / sizeof(header[0])),
				TEXT("Some hotkeys could not be registered (already in use):\r\n"));
			StringCchCopy(message, (int)(sizeof(message) / sizeof(message[0])), header);
			StringCchCat(message, (int)(sizeof(message) / sizeof(message[0])), warning);
			MessageBox(NULL, message, titleWarning, MB_OK | MB_ICONEXCLAMATION);
		}
	}

	while (GetMessage(&Msg, NULL, 0, 0)) {
		if (Msg.message == WM_HOTKEY) {
			DispatchMessage(&Msg);
			continue;
		}
		if (!IsDialogMessage(hWnd, &Msg)) {
			TranslateMessage(&Msg);
			DispatchMessage(&Msg);
		}
	}
	DestroyWindow(hWnd);

	return (int)(INT_PTR)Msg.wParam;
}
