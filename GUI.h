
//GUI.h
#pragma once
#pragma comment(lib,"d3d11.lib")
#include <d3d11.h>
#include "third_party/imgui/imgui.h"
#include "third_party/imgui/imgui_impl_win32.h"
#include "third_party/imgui/imgui_impl_dx11.h"
#include "third_party/imgui/imgui_internal.h"
#include "UI.h"
#include "tools/xor.h"


#define DIRECTINPUT_VERSION 0x0800
#define LOADER_BRAND "++"
#define WINDOW_WIDTH  785
#define WINDOW_HEIGHT 705

#include <dinput.h>
#include <tchar.h>
#include <string>
#include <thread>
#include <iostream>
#include <random>

#include <ShlObj.h>
#include <ShlObj_core.h>


namespace render
{
	extern ID3D11Device* g_pd3dDevice;
	extern ID3D11DeviceContext* g_pd3dDeviceContext;
	extern ID3D11RenderTargetView* g_mainRenderTargetView;
	extern IDXGISwapChain* g_pSwapChain;

	static UINT                    g_ResizeWidth = 0, g_ResizeHeight = 0;

	// HANDLES
	extern WNDCLASSEXW wc;
	extern HWND		  hwnd;

	// FUNCTIONS DECLARATIONS
	void BeginGUIDraw();
	void EndGUIDraw();
	void GUIShutDown();
	void GUIInit();

	// UI STYLES
	float BeginCenteredColumn(const char* id, float maxW);
	void EndCenteredColumn();

	// Soft container: draws lifted surface with shadow behind content
	void BeginSoftContainer(float padding = 24.0f, float rounding = 12.0f);
	void EndSoftContainer();

	// APPLICATION
	void SetupProgram();
	void DisplayProgram();

	// SYSTEM TRAY
	void AddTrayIcon();
	void RemoveTrayIcon();
	void HideToTray();
	void RestoreFromTray();
	bool ConsumeTrayLock();   // returns true once when Lock clicked
	bool ConsumeTrayQuit();   // returns true once when Quit clicked
	bool IsWindowVisible();
	void SetMinimizeToTray(bool enabled); // sync from ShellState each frame

	// THEME
	void ApplyDarkTheme();
	void ApplyLightTheme();
	void ApplyTheme(bool dark);

	// FONTS
	extern ImFont* Font15;
	extern ImFont* FontIcons;
	extern ImFont* FontSmall;   // 12px - captions, subtitles
	extern ImFont* FontRegular; // 15px - body text, labels
	extern ImFont* FontBold;    // 15px - bold labels
	extern ImFont* FontLarge;   // 18px - titles, headers

}


