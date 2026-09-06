#pragma once

#include "../Utils/Logger.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <iostream>
#include <string>
#include <vector>

#include "../Memory/GameData.h"
#include "../Memory/Hooks.h"
#include "../Memory/MinHook.h"
#include "../SDK/CChestBlockActor.h"
#include "../SDK/CClientInstance.h"
#include "../SDK/CClientInstanceScreenModel.h"
#include "../SDK/CEntity.h"
#include "../SDK/CGameMode.h"
#include "../SDK/CPacket.h"
#include "../Utils/HMath.h"
#include "../Utils/Target.h"
#include "../Utils/TextFormat.h"
#include "../Utils/Utils.h"
#include "../include/WinHttpClient.h"
#include "Command/CommandMgr.h"
#include "Config/ConfigManager.h"
#include "Menu/ClickGui.h"
#include "Menu/TabGui.h"
#include "Module/ModuleManager.h"
#include "ImmediateGui.h"

// Loader.cpp defines this flag. The compatibility shim uses it to stop the
// injector-connection thread when the archived signatures no longer match the
// running Minecraft build.
extern bool isRunning;

// Loader.cpp was written before Bedrock's large post-1.18 engine changes and
// immediately dereferences several signature results. On a current build, a
// missing signature can therefore turn into an access violation before Horion
// has a chance to say what went wrong. Intercept the Loader.cpp calls to the
// static GameData helpers so startup can fail closed and leave Minecraft alive.
class HorionCompatGameData {
private:
	struct SignatureCheck {
		const char* name;
		const char* pattern;
	};

	[[noreturn]] static void abortStartup(const char* reason) {
		logF("[compat] Startup stopped safely: %s", reason);
		logF("[compat] Minecraft was left running. See %%TEMP%%\\HorionCompat.log for this trace.");
		isRunning = false;
		MessageBoxA(nullptr,
			"This source build is not compatible with your current Minecraft Bedrock build yet.\n\n"
			"Minecraft was left running instead of crashing.\n"
			"Open %TEMP%\\HorionCompat.log and send the contents back so the missing signatures can be updated.",
			"Horion compatibility check",
			MB_OK | MB_ICONWARNING);
		ExitThread(0);
	}

public:
	static void initGameData(const SlimUtils::SlimModule* module, SlimUtils::SlimMem* slimMem, void* hDllInst) {
		logF("[compat] Beginning preflight signature scan");

		if (module == nullptr || module->ptrBase == 0)
			abortStartup("Minecraft.Windows.exe module could not be resolved");

		logF("[compat] Minecraft module base: %llX", module->ptrBase);

		// These are the first signatures the archived startup path relies on.
		// We intentionally check them before GameData::initGameData / Hooks::Init
		// because the old code dereferences some of them before checking for 0.
		const SignatureCheck checks[] = {
			{"ClientInstance", "48 8B 15 ? ? ? ? 4C 8B 02 4C 89 06 40 84 FF 74 ? 48 8B CD E8 ? ? ? ? 48 8B C6 48 8B 4C 24 ? 48 33 CC E8 ? ? ? ? 48 8B 5C 24 ? 48 8B 6C 24 ? 48 8B 74 24 ? 48 83 C4 ? 5F C3 B9 ? ? ? ? E8 ? ? ? ? CC E8 ? ? ? ? CC CC CC CC CC CC CC CC CC CC CC 48 89 5C 24 ? 48 89 6C 24 ? 56"},
			{"KeyMap", "48 8D 0D ?? ?? ?? ?? 89 1C B9"},
			{"GameMode vtable", "48 8D 05 ? ? ? ? 48 8B D9 48 89 01 8B FA 48 8B 89 ? ? ? ? 48 85 C9 74 ? 48 8B 01 BA ? ? ? ? FF 10 48 8B 8B"},
			{"BlockLegacy vtable", "48 8D 05 ? ? ? ? 48 89 01 4C 8B 72 ? 48 B9"},
			{"LocalPlayer vtable", "48 8D 05 ?? ?? ?? ?? 48 89 07 48 8D 8F ?? ?? ?? ?? 48 8B 87"},
			{"MoveInputHandler vtable", "48 8D 0D ? ? ? ? 49 89 48 ? 49 89 80 ? ? ? ? 49 89 80 ? ? ? ? 48 39 87 ? ? ? ? 74 20 48 8B 8F"},
			{"Player::tickWorld", "48 89 5C 24 ?? 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 ?? 48 81 EC ?? ?? ?? ?? 0F 29 B4 24 ?? ?? ?? ?? 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 45 ?? 48 89 55 ?? 48 8B F9"},
			{"RenderText", "48 8B C4 48 89 58 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 ? ? ? ? 48 81 EC ? ? ? ? 0F 29 70 B8 0F 29 78 A8 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 4C 8B FA 48 89 54 24 ? 4C 8B E9"}
		};

		bool allFound = true;
		for (const auto& check : checks) {
			const uintptr_t address = FindSignature(check.pattern);
			logF("[compat] %-24s %s @ %llX", check.name, address != 0 ? "FOUND" : "MISSING", address);
			if (address == 0)
				allFound = false;
		}

		if (!allFound)
			abortStartup("one or more archived 1.18-era signatures are missing");

		logF("[compat] Critical signatures matched; entering archived GameData initialization");
		::GameData::initGameData(module, slimMem, hDllInst);

		if (g_Data.getClientInstance() == nullptr)
			abortStartup("ClientInstance resolved to nullptr");

		logF("[compat] GameData initialization returned with ClientInstance=%llX", g_Data.getClientInstance());
	}

	// Forward the remaining static helpers used by Loader.cpp.
	static bool canUseMoveKeys() { return ::GameData::canUseMoveKeys(); }
	static bool isKeyDown(int key) { return ::GameData::isKeyDown(key); }
	static bool isKeyPressed(int key) { return ::GameData::isKeyPressed(key); }
	static bool shouldTerminate() { return ::GameData::shouldTerminate(); }
	static void terminate() { ::GameData::terminate(); }
	static bool shouldHide() { return ::GameData::shouldHide(); }
};

// Only translation units that include Loader.h use the compatibility wrapper;
// the underlying GameData implementation remains unchanged.
#define GameData HorionCompatGameData
