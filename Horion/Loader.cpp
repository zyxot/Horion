#include "Loader.h"
#include "Modern126Runtime.h"
#include "Modern126Gameplay.h"

SlimUtils::SlimMem mem;
const SlimUtils::SlimModule* gameModule;
bool isRunning = true;

#if defined _M_X64
#pragma comment(lib, "MinHook.x64.lib")
#elif defined _M_IX86
#pragma comment(lib, "MinHook.x86.lib")
#endif

#ifndef _MSC_VER
#define _MSC_VER "unk"
#endif

// The archived Loader.cpp initialized every 1.18-era hook immediately.  On
// current Bedrock that means dereferencing dead signatures before Horion even
// reaches its menu.  The modern branch now boots into a small 1.26 runtime
// bridge first.  It only installs pass-through hooks whose addresses were
// verified on Minecraft.Windows.exe 1.26.45.1.  The old hook table stays out of
// the process until each subsystem has been ported and validated.
DWORD WINAPI start(LPVOID lpParam) {
	logF("Starting up...");
	logF("MSC v%i at %s", _MSC_VER, __TIMESTAMP__);

	DWORD procId = GetCurrentProcessId();
	if (!mem.Open(procId, SlimUtils::ProcessAccess::Full)) {
		logF("Failed to open process, error-code: %i", GetLastError());
		return 1;
	}
	gameModule = mem.GetModule(L"Minecraft.Windows.exe");
	if (gameModule == nullptr || gameModule->ptrBase == 0) {
		logF("Minecraft.Windows.exe module could not be resolved");
		return 1;
	}

	const MH_STATUS minHookStatus = MH_Initialize();
	if (minHookStatus != MH_OK && minHookStatus != MH_ERROR_ALREADY_INITIALIZED) {
		logF("MH_Initialize failed: %i", minHookStatus);
		return 1;
	}

	if (Modern126Runtime::tryStart(static_cast<HMODULE>(lpParam))) {
		Modern126Gameplay::start(Modern126Runtime::clientInstance, Modern126Runtime::keyMap);
		logF("[modern] 1.26 runtime core is ACTIVE");
		logF("[modern] Waiting for render/update callbacks; CTRL+L unloads the DLL");
		ExitThread(0);
	}

	// If the current runtime bridge cannot prove that it is on the expected
	// Bedrock generation, fall back to the crash-safe diagnostics in Loader.h.
	// That path intentionally exits the startup thread rather than running stale
	// hooks.
	logF("[modern] Runtime bridge did not activate; entering compatibility diagnostics");
	GameData::initGameData(gameModule, &mem, static_cast<HMODULE>(lpParam));
	ExitThread(0);
}

BOOL __stdcall DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID) {
	switch (ul_reason_for_call) {
	case DLL_PROCESS_ATTACH:
		DisableThreadLibraryCalls(hModule);
		CreateThread(nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(start), hModule, 0, nullptr);
		break;

	case DLL_PROCESS_DETACH:
		isRunning = false;

		if (Modern126Runtime::isSelected()) {
			Modern126Gameplay::shutdown();
			Modern126Runtime::shutdownHooks();
			logF("[modern] Runtime bridge detached");
			Logger::Disable();
			MH_Uninitialize();
			break;
		}

		// Diagnostic-only startup never enables the archived hook table.  Keep
		// detach deliberately small so stale GameData/SDK layouts are not touched.
		logF("Removing logger");
		Logger::Disable();
		MH_Uninitialize();
		break;
	}
	return TRUE;
}
