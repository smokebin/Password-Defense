
//GUI.h
#pragma once
#pragma comment(lib,"d3d11.lib")
#include <d3d11.h>
#include "third_party/imgui/imgui.h"
#include "third_party/imgui/imgui_impl_win32.h"
#include "third_party/imgui/imgui_impl_dx11.h"
#include "third_party/imgui/imgui_internal.h"
#include "UI.h"


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

	extern WNDCLASSEXW wc;
	extern HWND		  hwnd;

	void BeginGUIDraw();
	void EndGUIDraw();
	void GUIShutDown();
	void GUIInit();

	float BeginCenteredColumn(const char* id, float maxW);
	void EndCenteredColumn();

	// lifted surface with layered shadow
	void BeginSoftContainer(float padding = 24.0f, float rounding = 12.0f);
	void EndSoftContainer();

	void SetupProgram();
	void DisplayProgram();

	void AddTrayIcon();
	void RemoveTrayIcon();
	void HideToTray();
	void RestoreFromTray();
	bool ConsumeTrayLock();   // true once when Lock clicked
	bool IsWindowVisible();
	void SetMinimizeToTray(bool enabled); // called each frame from application.cpp

	void ApplyDarkTheme();
	void ApplyLightTheme();
	void ApplyTheme(bool dark);

	extern ImFont* Font15;       // alias for FontRegular
	extern ImFont* FontIcons;
	extern ImFont* FontSmall;   // 13px
	extern ImFont* FontRegular; // 15px
	extern ImFont* FontBold;    // 20px segoe bold
	extern ImFont* FontLarge;   // 17px

}


