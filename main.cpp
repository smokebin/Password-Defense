#include "GUI.h"
#include "tools/save.h"

#define SODIUM_STATIC
#include <sodium.h>

int main()
{
	// Initialize libsodium
	if (sodium_init() < 0) {
		MessageBoxA(nullptr, "Failed to initialize libsodium crypto library", "Fatal Error", MB_ICONERROR);
		return 1;
	}

	// hide files
	SHELLSTATE ss;
	ZeroMemory(&ss, sizeof(ss));
	ss.fShowAllObjects = FALSE;
	ss.fShowSysFiles = FALSE;
	SHGetSetSettings(&ss, SSF_SHOWALLOBJECTS | SSF_SHOWSYSFILES | SSF_SHOWSUPERHIDDEN, TRUE);

	// hide console
	ShowWindow(GetConsoleWindow(), SW_HIDE);

	// Initialize config
	cfg::init();
	cfg::register_self_destruct_handler();

	// setup Menu
	render::SetupProgram();

	// main Login loop
	MSG msg;
	ZeroMemory(&msg, sizeof(msg));
	while (msg.message != WM_QUIT)
	{
		// setup Input
		if (::PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
		{
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
			continue;
		}

		// Skip rendering when hidden to tray (save CPU)
		if (!render::IsWindowVisible()) { Sleep(100); continue; }

		render::BeginGUIDraw();

		render::DisplayProgram();

		render::EndGUIDraw();
	}

	render::GUIShutDown();
	return 0;
}
