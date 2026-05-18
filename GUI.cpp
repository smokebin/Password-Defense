//gui.cpp
#include <Windows.h>
#include <shellapi.h>
#pragma comment(lib, "dwmapi.lib")
#include <dwmapi.h>
#include "GUI.h"
#include "tools/save.h"

// ---- System tray constants & state ----
#define WM_TRAYICON      (WM_APP + 1)
#define IDM_TRAY_OPEN    40001
#define IDM_TRAY_LOCK    40002
#define IDM_TRAY_QUIT    40003

static NOTIFYICONDATAW g_nid = {};
static bool g_tray_icon_added   = false;
static bool g_tray_lock_requested = false;
static bool g_tray_quit_requested = false;
static bool g_window_visible    = true;
static bool g_minimize_to_tray  = false;  // cached from ShellState each frame
#include "icons/IconsMaterialDesignIcons.h"
#include "icons/mdi_font_data.h"

#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb_image.h"
#include "favicon.h"
#include "sidebar_icons.h"

// forward declarations of helper functions
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace render
{
	// DIRECTX POINTERS
	ID3D11Device* g_pd3dDevice = nullptr;
	ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
	IDXGISwapChain* g_pSwapChain = nullptr;
	ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

	// HANDLES
	WNDCLASSEXW wc = { NULL };
	HWND		 hwnd = nullptr;

	// FONTS
	ImFont* Font15 = nullptr;
	ImFont* FontIcons = nullptr;
	ImFont* FontSmall = nullptr;   // 12px - captions, subtitles
	ImFont* FontRegular = nullptr; // 15px - body text, labels
	ImFont* FontBold = nullptr;    // 15px - bold labels
	ImFont* FontLarge = nullptr;   // 18px - titles, headers

}

float render::BeginCenteredColumn(const char* id, float maxW)
{
	float avail = ImGui::GetContentRegionAvail().x;
	float w = (avail > maxW) ? maxW : avail;

	float x = ImGui::GetCursorPosX();
	if (avail > w)
		ImGui::SetCursorPosX(x + (avail - w) * 0.5f);

	ImGui::BeginChild(id, ImVec2(w, 0), false); // height 0 = use remaining
	return w;
}

void render::EndCenteredColumn()
{
	ImGui::EndChild();
}

// Soft container state (stack-based for nesting)
static struct SoftContainerState {
	ImVec2 contentMin;
	float padding;
	float rounding;
	bool active;
	int bgChannelIdx;
} g_softContainer = {};

// Theme-aware colors for soft container and modals
static ImU32 GetSoftContainerShadowColor(int alpha)
{
	// Shadows are always dark, but slightly stronger on light theme
	if (cfg::is_dark_theme())
		return IM_COL32(0, 0, 0, alpha);
	else
		return IM_COL32(0, 0, 0, (int)(alpha * 1.3f));
}

static ImU32 GetSoftContainerSurfaceColor()
{
	if (cfg::is_dark_theme()) {
		auto c = cfg::get_soft_container_dark();
		return IM_COL32((int)(c.r*255), (int)(c.g*255), (int)(c.b*255), (int)(c.a*255));
	} else {
		auto c = cfg::get_soft_container_light();
		return IM_COL32((int)(c.r*255), (int)(c.g*255), (int)(c.b*255), (int)(c.a*255));
	}
}

static ImU32 GetModalPopupBgColor()
{
	if (cfg::is_dark_theme())
		return IM_COL32(36, 35, 39, 255);
	else
		return IM_COL32(250, 250, 252, 255);
}

void render::BeginSoftContainer(float padding, float rounding)
{
	g_softContainer.padding = padding;
	g_softContainer.rounding = rounding;
	g_softContainer.active = true;

	// Where the container starts (screen space)
	g_softContainer.contentMin = ImGui::GetCursorScreenPos();

	ImDrawList* dl = ImGui::GetWindowDrawList();

	// Prevent shadow/bg bleeding outside the window
	dl->PushClipRectFullScreen();

	// 0 = background, 1 = content
	dl->ChannelsSplit(2);
	dl->ChannelsSetCurrent(1);

	// Inner padding
	ImGui::SetCursorScreenPos(ImVec2(
		g_softContainer.contentMin.x + padding,
		g_softContainer.contentMin.y + padding
	));

	ImGui::BeginGroup();
}

void render::EndSoftContainer()
{
	if (!g_softContainer.active) return;

	ImGui::EndGroup();

	ImVec2 contentMax = ImGui::GetItemRectMax();

	const float pad = g_softContainer.padding;
	const float rounding = g_softContainer.rounding;

	ImVec2 boxMin = g_softContainer.contentMin;
	ImVec2 boxMax = ImVec2(contentMax.x + pad, contentMax.y + pad);

	ImDrawList* dl = ImGui::GetWindowDrawList();

	// background channel
	dl->ChannelsSetCurrent(0);

	// --- SHADOW: symmetric, subtle, "lifted surface" (theme-aware) ---
	auto shadow_pass = [&](float grow, float offY, int a, float roundAdd)
		{
			float g = grow * 0.5f;
			dl->AddRectFilled(
				ImVec2(boxMin.x - g, boxMin.y + offY),
				ImVec2(boxMax.x + g, boxMax.y + g + offY),
				GetSoftContainerShadowColor(a),
				rounding + roundAdd
			);
		};

	shadow_pass(16.0f, 5.0f, 10, 10.0f); // outer
	shadow_pass(10.0f, 3.0f, 18, 6.0f);  // mid
	shadow_pass(6.0f, 2.0f, 26, 2.0f);  // inner

	// --- SURFACE (theme-aware) ---
	const ImU32 surfaceCol = GetSoftContainerSurfaceColor();
	dl->AddRectFilled(boxMin, boxMax, surfaceCol, rounding);

	// Optional faint border (usually helps in dark themes)
	// dl->AddRect(boxMin, boxMax, IM_COL32(255,255,255,18), rounding);

	// merge channels back
	dl->ChannelsMerge();

	dl->ChannelsSetCurrent(0);

	dl->PopClipRect();
 
	// advance cursor past container
// Submit an item to advance layout instead of jumping cursor outside boundaries.
// This keeps ImGui happy in newer versions (especially inside scrolling children).
	ImGui::SetCursorScreenPos(boxMin);

	// Width: 0 means "use available width" for the layout advance
	// Height: container height + spacing
	const float advance_h = (boxMax.y - boxMin.y) + 8.0f;
	ImGui::Dummy(ImVec2(0.0f, advance_h));

	g_softContainer.active = false;
}


// ---- System tray functions ----

void render::AddTrayIcon()
{
	if (g_tray_icon_added) return;
	ZeroMemory(&g_nid, sizeof(g_nid));
	g_nid.cbSize = sizeof(g_nid);
	g_nid.hWnd   = hwnd;
	g_nid.uID    = 1;
	g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
	g_nid.uCallbackMessage = WM_TRAYICON;
	g_nid.hIcon  = LoadIcon(wc.hInstance, MAKEINTRESOURCE(1));
	wcscpy_s(g_nid.szTip, L"Password Defense");
	Shell_NotifyIconW(NIM_ADD, &g_nid);
	g_tray_icon_added = true;
}

void render::RemoveTrayIcon()
{
	if (!g_tray_icon_added) return;
	Shell_NotifyIconW(NIM_DELETE, &g_nid);
	g_tray_icon_added = false;
}

void render::HideToTray()
{
	::ShowWindow(hwnd, SW_HIDE);
	g_window_visible = false;
}

void render::RestoreFromTray()
{
	::ShowWindow(hwnd, SW_SHOW);
	::SetForegroundWindow(hwnd);
	g_window_visible = true;
}

bool render::ConsumeTrayLock()
{
	bool v = g_tray_lock_requested;
	g_tray_lock_requested = false;
	return v;
}

bool render::ConsumeTrayQuit()
{
	bool v = g_tray_quit_requested;
	g_tray_quit_requested = false;
	return v;
}

bool render::IsWindowVisible()
{
	return g_window_visible;
}

void render::SetMinimizeToTray(bool enabled)
{
	g_minimize_to_tray = enabled;
}

void render::GUIInit()
{
	// Initialize tray behavior from config
	g_minimize_to_tray = cfg::get_minimize_to_tray();

	// Show the window (gated by start_minimized)
	bool start_hidden = cfg::get_start_minimized() && g_minimize_to_tray;
	if (!start_hidden)
	{
		::ShowWindow(hwnd, SW_SHOW);
	}
	else
	{
		g_window_visible = false;
	}
	::UpdateWindow(hwnd);

	//CONTEXT
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.LogFilename = nullptr;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
	io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

	// Font sizes
	const float sizeSmall = 13.0f;
	const float sizeRegular = 15.0f;
	const float sizeLarge = 17.0f;
	const float sizeBold = 20.0f;

	// Get system fonts path
	TCHAR szPath[MAX_PATH];
	std::string bahnschriftPath;
	std::string segoeuiBoldPath;
	if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_FONTS, NULL, SHGFP_TYPE_CURRENT, szPath)))
	{
        #ifdef UNICODE
            std::wstring wpath = std::wstring(szPath) + L"\\bahnschrift.ttf";
            int len = WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, NULL, 0, NULL, NULL);
            if (len > 0) {
                std::vector<char> buf(len);
                WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, &buf[0], len, NULL, NULL);
                bahnschriftPath = &buf[0];
            }
            std::wstring wbold = std::wstring(szPath) + L"\\segoeuib.ttf";
            len = WideCharToMultiByte(CP_UTF8, 0, wbold.c_str(), -1, NULL, 0, NULL, NULL);
            if (len > 0) {
                std::vector<char> buf(len);
                WideCharToMultiByte(CP_UTF8, 0, wbold.c_str(), -1, &buf[0], len, NULL, NULL);
                segoeuiBoldPath = &buf[0];
            }
        #else
            bahnschriftPath = std::string(szPath) + "\\bahnschrift.ttf";
            segoeuiBoldPath = std::string(szPath) + "\\segoeuib.ttf";
        #endif
	}

	// Icon config (will be reused with MergeMode)
	static const ImWchar mdi_ranges[] = { ICON_MIN_MDI, ICON_MAX_MDI, 0 };
	ImFontConfig icon_cfg;
	icon_cfg.MergeMode = true;
	icon_cfg.PixelSnapH = true;
	icon_cfg.OversampleH = 3;
	icon_cfg.OversampleV = 3;

	// Helper lambda to load font + merge icons
	auto loadFontWithIcons = [&](float size) -> ImFont* {
		ImFont* font = nullptr;
		if (!bahnschriftPath.empty())
			font = io.Fonts->AddFontFromFileTTF(bahnschriftPath.c_str(), size);
		if (!font)
			font = io.Fonts->AddFontDefault();
		// Merge MDI icons slightly larger for visual weight (static data — must not be freed by ImGui)
		icon_cfg.GlyphMinAdvanceX = size;
		icon_cfg.FontDataOwnedByAtlas = false;
		icon_cfg.GlyphOffset.y = 1.0f;
		io.Fonts->AddFontFromMemoryTTF((void*)mdi_font_data, mdi_font_size, size + 4.0f, &icon_cfg, mdi_ranges);
		icon_cfg.GlyphOffset.y = 0.0f;
		icon_cfg.FontDataOwnedByAtlas = true;
		return font;
	};

	// Load all font sizes with icons merged
	FontSmall = loadFontWithIcons(sizeSmall);
	FontRegular = loadFontWithIcons(sizeRegular);
	FontLarge = loadFontWithIcons(sizeLarge);

	// Bold font (Segoe UI Bold) at bold size with icons merged
	{
		ImFont* font = nullptr;
		if (!segoeuiBoldPath.empty())
			font = io.Fonts->AddFontFromFileTTF(segoeuiBoldPath.c_str(), sizeBold);
		if (!font)
			font = io.Fonts->AddFontDefault();
		icon_cfg.GlyphMinAdvanceX = sizeBold;
		icon_cfg.FontDataOwnedByAtlas = false;
		icon_cfg.GlyphOffset.y = 1.0f;
		io.Fonts->AddFontFromMemoryTTF((void*)mdi_font_data, mdi_font_size, sizeBold + 4.0f, &icon_cfg, mdi_ranges);
		icon_cfg.GlyphOffset.y = 0.0f;
		icon_cfg.FontDataOwnedByAtlas = true;
		FontBold = font;
	}

	// Keep Font15 as alias to FontRegular for backwards compatibility
	Font15 = FontRegular;

	ImGuiStyle& style = ImGui::GetStyle();
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		style.WindowRounding = 0.0f;
		style.Colors[ImGuiCol_WindowBg].w = 1.0f;

	}


	// Style settings (theme-independent)
	style.Colors[ImGuiCol_NavCursor].w = 0.0f; // Hide navigation cursor
	style.ScrollbarSize = 4.0f;
	style.WindowPadding = ImVec2(5, 5);
	style.CellPadding = ImVec2(3, 8);
	style.FrameBorderSize = 1.0f;

	// Setup Platform/Renderer backends
	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

	// Apply theme from config
	ApplyTheme(cfg::is_dark_theme());

	// Initialize favicon cache
	favicon::Init();
	sidebar_icons::Init();

	// Add system tray icon
	AddTrayIcon();

	return;
}

void render::GUIShutDown()
{
	//CLEANUP & SHUTDOWN
	RemoveTrayIcon();
	favicon::Shutdown();
	sidebar_icons::Shutdown();
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	CleanupDeviceD3D();
	::DestroyWindow(hwnd);
	::UnregisterClassW(wc.lpszClassName, wc.hInstance);
	return;
}

void render::BeginGUIDraw()
{

	// Start the Dear ImGui frame
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);

}

void render::EndGUIDraw()
{

	if (g_ResizeWidth != 0 && g_ResizeHeight != 0 && g_pSwapChain)
	{
		CleanupRenderTarget();
		g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
		g_ResizeWidth = 0;
		g_ResizeHeight = 0;
		CreateRenderTarget();
	}

	// Render toasts on top of everything
	ui::RenderToasts();

	// Clipboard auto-clear tick
	ui::TickClipboardClear();

	// Rendering
	ImGuiIO& io = ImGui::GetIO();
	float clearColor[4] = { 255.0f,255.0f,255.0f,255.0f };
	ImGui::Render();
	g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
	g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clearColor);
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

	// Update and Render additional Platform Windows
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		ImGui::UpdatePlatformWindows();
		ImGui::RenderPlatformWindowsDefault();
	}

	// Present
	g_pSwapChain->Present(1, 0);   // Present with vsync
	//g_pSwapChain->Present(0, 0); // Present without vsync

}

void render::SetupProgram()
{
	// Window dimensions
	const int winW = WINDOW_WIDTH;
	const int winH = WINDOW_HEIGHT;

	// Center on screen
	const int screenW = GetSystemMetrics(SM_CXSCREEN);
	const int screenH = GetSystemMetrics(SM_CYSCREEN);
	const int posX = (screenW - winW) / 2;
	const int posY = (screenH - winH) / 2;

	wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"Password Defense", nullptr };
	::RegisterClassExW(&wc);
	hwnd = ::CreateWindowW(wc.lpszClassName, L"Password Defense", WS_OVERLAPPEDWINDOW, posX, posY, winW, winH, nullptr, nullptr, wc.hInstance, nullptr);

	// Set window icon (from resource)
	HICON hIcon = LoadIcon(wc.hInstance, MAKEINTRESOURCE(1));
	if (hIcon) {
		SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
		SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
	}

	// Initialize Direct3D
	if (!CreateDeviceD3D(hwnd))
	{
		CleanupDeviceD3D();
		::UnregisterClassW(wc.lpszClassName, wc.hInstance);
		return;
	}

	// Curosr
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);

	// Colors
	GUIInit();

	return;
}

// helper functions
bool CreateDeviceD3D(HWND hWnd)
{
	// Setup swap chain
	DXGI_SWAP_CHAIN_DESC sd;
	ZeroMemory(&sd, sizeof(sd));
	sd.BufferCount = 2;
	sd.BufferDesc.Width = 0;
	sd.BufferDesc.Height = 0;
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.BufferDesc.RefreshRate.Numerator = 60;
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.OutputWindow = hWnd;
	sd.SampleDesc.Count = 1;
	sd.SampleDesc.Quality = 0;
	sd.Windowed = TRUE;
	sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	UINT createDeviceFlags = 0;
	//createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
	D3D_FEATURE_LEVEL featureLevel;
	const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
	HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &render::g_pSwapChain, &render::g_pd3dDevice, &featureLevel, &render::g_pd3dDeviceContext);
	if (res == DXGI_ERROR_UNSUPPORTED) // Try high-performance WARP software driver if hardware is not available.
		res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &render::g_pSwapChain, &render::g_pd3dDevice, &featureLevel, &render::g_pd3dDeviceContext);
	if (res != S_OK)
		return false;

	CreateRenderTarget();
	return true;
}

void CleanupDeviceD3D()
{
	CleanupRenderTarget();
	if (render::g_pSwapChain) { render::g_pSwapChain->Release(); render::g_pSwapChain = nullptr; }
	if (render::g_pd3dDeviceContext) { render::g_pd3dDeviceContext->Release(); render::g_pd3dDeviceContext = nullptr; }
	if (render::g_pd3dDevice) { render::g_pd3dDevice->Release(); render::g_pd3dDevice = nullptr; }
}

void CreateRenderTarget()
{
	ID3D11Texture2D* pBackBuffer;
	render::g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
	render::g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &render::g_mainRenderTargetView);
	pBackBuffer->Release();
}

void CleanupRenderTarget()
{
	if (render::g_mainRenderTargetView) { render::g_mainRenderTargetView->Release(); render::g_mainRenderTargetView = nullptr; }
}

// ============================================================
// THEME FUNCTIONS
// ============================================================

void render::ApplyDarkTheme()
{
	ImGuiStyle& style = ImGui::GetStyle();

	// Window
	style.Colors[ImGuiCol_WindowBg] = ImColor(28, 27, 30, 255);
	style.Colors[ImGuiCol_ChildBg] = ImColor(30, 30, 30, 0);
	style.Colors[ImGuiCol_PopupBg] = ImColor(26, 26, 28, 255);
	style.Colors[ImGuiCol_FrameBg] = ImColor(0, 0, 0, 0);
	style.Colors[ImGuiCol_Border] = ImColor(40, 40, 40, 255);

	// Scrollbar
	style.Colors[ImGuiCol_ScrollbarBg] = ImColor(40, 40, 40, 0);
	style.Colors[ImGuiCol_ScrollbarGrab] = ImColor(60, 60, 60, 255);
	style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImColor(80, 80, 80, 255);
	style.Colors[ImGuiCol_ScrollbarGrabActive] = ImColor(100, 100, 100, 255);

	// Buttons
	style.Colors[ImGuiCol_Button] = ImColor(42, 42, 42, 102);
	style.Colors[ImGuiCol_ButtonHovered] = ImColor(53, 54, 61, 150);
	style.Colors[ImGuiCol_ButtonActive] = ImColor(53, 54, 61, 100);

	// Headers
	style.Colors[ImGuiCol_Header] = ImColor(255, 255, 255, 0);
	style.Colors[ImGuiCol_HeaderHovered] = ImColor(53, 54, 61, 150);
	style.Colors[ImGuiCol_HeaderActive] = ImColor(53, 54, 61, 100);

	// Text
	style.Colors[ImGuiCol_Text] = ImColor(255, 255, 255, 255);
	style.Colors[ImGuiCol_TextDisabled] = ImColor(128, 128, 128, 255);
	style.Colors[ImGuiCol_InputTextCursor] = ImColor(255, 255, 255, 255);

	// Table
	style.Colors[ImGuiCol_TableHeaderBg] = ImColor(36, 35, 39, 255);
	style.Colors[ImGuiCol_TableBorderStrong] = ImColor(50, 50, 55, 255);
	style.Colors[ImGuiCol_TableBorderLight] = ImColor(40, 40, 45, 255);
	style.Colors[ImGuiCol_TableRowBg] = ImColor(0, 0, 0, 0);
	style.Colors[ImGuiCol_TableRowBgAlt] = ImColor(255, 255, 255, 10);

	// Title bar (Windows integration)
	BOOL useDarkMode = TRUE;
	DwmSetWindowAttribute(hwnd, 20, &useDarkMode, sizeof(useDarkMode));
	COLORREF captionColor = RGB(28, 27, 30);
	DwmSetWindowAttribute(hwnd, 35, &captionColor, sizeof(captionColor));
}

void render::ApplyLightTheme()
{
	ImGuiStyle& style = ImGui::GetStyle();

	// Window
	style.Colors[ImGuiCol_WindowBg] = ImColor(245, 245, 247, 255);
	style.Colors[ImGuiCol_ChildBg] = ImColor(255, 255, 255, 0);
	style.Colors[ImGuiCol_PopupBg] = ImColor(255, 255, 255, 255);
	style.Colors[ImGuiCol_FrameBg] = ImColor(0, 0, 0, 0);
	style.Colors[ImGuiCol_Border] = ImColor(200, 200, 200, 255);

	// Scrollbar
	style.Colors[ImGuiCol_ScrollbarBg] = ImColor(245, 245, 245, 0);
	style.Colors[ImGuiCol_ScrollbarGrab] = ImColor(180, 180, 180, 255);
	style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImColor(150, 150, 150, 255);
	style.Colors[ImGuiCol_ScrollbarGrabActive] = ImColor(120, 120, 120, 255);

	// Buttons
	style.Colors[ImGuiCol_Button] = ImColor(220, 220, 220, 255);
	style.Colors[ImGuiCol_ButtonHovered] = ImColor(200, 200, 200, 255);
	style.Colors[ImGuiCol_ButtonActive] = ImColor(180, 180, 180, 255);

	// Headers
	style.Colors[ImGuiCol_Header] = ImColor(0, 0, 0, 0);
	style.Colors[ImGuiCol_HeaderHovered] = ImColor(220, 220, 225, 200);
	style.Colors[ImGuiCol_HeaderActive] = ImColor(200, 200, 205, 200);

	// Text
	style.Colors[ImGuiCol_Text] = ImColor(30, 30, 30, 255);
	style.Colors[ImGuiCol_TextDisabled] = ImColor(140, 140, 140, 255);
	style.Colors[ImGuiCol_InputTextCursor] = ImColor(30, 30, 30, 255);

	// Table
	style.Colors[ImGuiCol_TableHeaderBg] = ImColor(235, 235, 240, 255);
	style.Colors[ImGuiCol_TableBorderStrong] = ImColor(200, 200, 205, 255);
	style.Colors[ImGuiCol_TableBorderLight] = ImColor(215, 215, 220, 255);
	style.Colors[ImGuiCol_TableRowBg] = ImColor(0, 0, 0, 0);
	style.Colors[ImGuiCol_TableRowBgAlt] = ImColor(0, 0, 0, 10);

	// Title bar (Windows integration)
	BOOL useDarkMode = FALSE;
	DwmSetWindowAttribute(hwnd, 20, &useDarkMode, sizeof(useDarkMode));
	COLORREF captionColor = RGB(245, 245, 247);
	DwmSetWindowAttribute(hwnd, 35, &captionColor, sizeof(captionColor));
}

void render::ApplyTheme(bool dark)
{
	if (dark)
		ApplyDarkTheme();
	else
		ApplyLightTheme();
}

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0 // from Windows SDK 8.1+ headers
#endif

// forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Win32 message handler
// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
		return true;

	switch (msg)
	{
	case WM_SETCURSOR:
		if (LOWORD(lParam) == HTCLIENT)
		{
			SetCursor(LoadCursor(NULL, IDC_ARROW));
			return TRUE;
		}
		break;
	case WM_SIZE:
		if (wParam == SIZE_MINIMIZED)
		{
			if (g_minimize_to_tray)
			{
				render::HideToTray();
				return 0;
			}
			return 0;
		}
		render::g_ResizeWidth = (UINT)LOWORD(lParam); // Queue resize
		render::g_ResizeHeight = (UINT)HIWORD(lParam);
		return 0;
	case WM_CLOSE:
		if (g_minimize_to_tray)
		{
			render::HideToTray();
			return 0;
		}
		break; // fall through to DefWindowProc → WM_DESTROY
	case WM_SYSCOMMAND:
		if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
			return 0;
		break;
	case WM_DESTROY:
		::PostQuitMessage(0);
		return 0;
	case WM_TRAYICON:
		if (lParam == WM_LBUTTONDBLCLK)
		{
			render::RestoreFromTray();
		}
		else if (lParam == WM_RBUTTONUP)
		{
			POINT pt;
			GetCursorPos(&pt);
			HMENU hMenu = CreatePopupMenu();
			AppendMenuW(hMenu, MF_STRING, IDM_TRAY_OPEN, L"Open");
			AppendMenuW(hMenu, MF_STRING, IDM_TRAY_LOCK, L"Lock Vault");
			AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
			AppendMenuW(hMenu, MF_STRING, IDM_TRAY_QUIT, L"Quit");
			SetForegroundWindow(hWnd);
			TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, nullptr);
			DestroyMenu(hMenu);
		}
		return 0;
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDM_TRAY_OPEN:
			render::RestoreFromTray();
			return 0;
		case IDM_TRAY_LOCK:
			render::RestoreFromTray();
			g_tray_lock_requested = true;
			return 0;
		case IDM_TRAY_QUIT:
			g_tray_quit_requested = true;
			return 0;
		}
		break;
	case WM_DPICHANGED:
		if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DpiEnableScaleViewports)
		{
			const RECT* suggested_rect = (RECT*)lParam;
			::SetWindowPos(hWnd, nullptr, suggested_rect->left, suggested_rect->top, suggested_rect->right - suggested_rect->left, suggested_rect->bottom - suggested_rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
		}
		break;
	}
	return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
