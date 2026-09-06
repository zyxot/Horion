#pragma once

#include "../Utils/Logger.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <winver.h>

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

#pragma comment(lib, "Version.lib")

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

	static void logMinecraftBinaryInfo() {
		HMODULE gameModuleHandle = GetModuleHandleA("Minecraft.Windows.exe");
		if (gameModuleHandle == nullptr) {
			logF("[compat] Minecraft binary handle unavailable");
			return;
		}

		char path[MAX_PATH] = {};
		if (GetModuleFileNameA(gameModuleHandle, path, MAX_PATH) == 0) {
			logF("[compat] Could not resolve Minecraft executable path (error %lu)", GetLastError());
			return;
		}

		logF("[compat] Minecraft executable: %s", path);

		DWORD dummy = 0;
		DWORD versionSize = GetFileVersionInfoSizeA(path, &dummy);
		if (versionSize == 0) {
			logF("[compat] Minecraft file version unavailable (error %lu)", GetLastError());
			return;
		}

		std::vector<unsigned char> versionData(versionSize);
		if (!GetFileVersionInfoA(path, 0, versionSize, versionData.data())) {
			logF("[compat] GetFileVersionInfo failed (error %lu)", GetLastError());
			return;
		}

		VS_FIXEDFILEINFO* fixedInfo = nullptr;
		UINT fixedInfoSize = 0;
		if (!VerQueryValueA(versionData.data(), "\\", reinterpret_cast<void**>(&fixedInfo), &fixedInfoSize) ||
			fixedInfo == nullptr || fixedInfoSize < sizeof(VS_FIXEDFILEINFO)) {
			logF("[compat] Minecraft version resource could not be read");
			return;
		}

		logF("[compat] Minecraft file version: %u.%u.%u.%u",
			HIWORD(fixedInfo->dwFileVersionMS),
			LOWORD(fixedInfo->dwFileVersionMS),
			HIWORD(fixedInfo->dwFileVersionLS),
			LOWORD(fixedInfo->dwFileVersionLS));
	}

	static int scanSignatures(const char* label, const SignatureCheck* checks, size_t count) {
		logF("[compat] ---- %s ----", label);
		int found = 0;
		for (size_t i = 0; i < count; ++i) {
			const uintptr_t address = FindSignature(checks[i].pattern);
			logF("[compat] %-32s %s @ %llX", checks[i].name, address != 0 ? "FOUND" : "MISSING", address);
			if (address != 0)
				++found;
		}
		logF("[compat] %s: %i/%llu candidates matched", label, found, static_cast<unsigned long long>(count));
		return found;
	}

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
		logMinecraftBinaryInfo();

		// First determine whether the archived Horion 1.18-era memory model still
		// resembles the running build. Never dereference these results here.
		const SignatureCheck archivedChecks[] = {
			{"ClientInstance (archived)", "48 8B 15 ? ? ? ? 4C 8B 02 4C 89 06 40 84 FF 74 ? 48 8B CD E8 ? ? ? ? 48 8B C6 48 8B 4C 24 ? 48 33 CC E8 ? ? ? ? 48 8B 5C 24 ? 48 8B 6C 24 ? 48 8B 74 24 ? 48 83 C4 ? 5F C3 B9 ? ? ? ? E8 ? ? ? ? CC E8 ? ? ? ? CC CC CC CC CC CC CC CC CC CC CC 48 89 5C 24 ? 48 89 6C 24 ? 56"},
			{"KeyMap (archived)", "48 8D 0D ?? ?? ?? ?? 89 1C B9"},
			{"GameMode vtable (archived)", "48 8D 05 ? ? ? ? 48 8B D9 48 89 01 8B FA 48 8B 89 ? ? ? ? 48 85 C9 74 ? 48 8B 01 BA ? ? ? ? FF 10 48 8B 8B"},
			{"BlockLegacy vtable (archived)", "48 8D 05 ? ? ? ? 48 89 01 4C 8B 72 ? 48 B9"},
			{"LocalPlayer vtable (archived)", "48 8D 05 ?? ?? ?? ?? 48 89 07 48 8D 8F ?? ?? ?? ?? 48 8B 87"},
			{"MoveInput vtable (archived)", "48 8D 0D ? ? ? ? 49 89 48 ? 49 89 80 ? ? ? ? 49 89 80 ? ? ? ? 48 39 87 ? ? ? ? 74 20 48 8B 8F"},
			{"Player::tickWorld (archived)", "48 89 5C 24 ?? 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 ?? 48 81 EC ?? ?? ?? ?? 0F 29 B4 24 ?? ?? ?? ?? 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 45 ?? 48 89 55 ?? 48 8B F9"},
			{"RenderText (archived)", "48 8B C4 48 89 58 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 ? ? ? ? 48 81 EC ? ? ? ? 0F 29 70 B8 0F 29 78 A8 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 4C 8B FA 48 89 54 24 ? 4C 8B E9"}
		};

		const int archivedFound = scanSignatures(
			"Archived Horion 1.18 signature set",
			archivedChecks,
			sizeof(archivedChecks) / sizeof(archivedChecks[0]));

		if (archivedFound != static_cast<int>(sizeof(archivedChecks) / sizeof(archivedChecks[0]))) {
			// The first diagnostic set was taken from a Horion-derived client targeting
			// Bedrock 1.21.130. Keep it as a historical bridge because one GameMode
			// shape still survives in 1.26.45.1.
			const SignatureCheck bridgeCandidates[] = {
				{"ClientInstance candidate", "48 89 0D ? ? ? ? 48 89 0D ? ? ? ? 48 85 C0 74 ? 48 8B C8 E8 ? ? ? ? 48 8B 0D ? ? ? ?"},
				{"Key input hook candidate", "48 83 EC ? 0F B6 C1 4C 8D 05"},
				{"GameMode vtable candidate", "48 8D 05 ? ? ? ? 48 89 01 48 89 51 ? 48 C7 41 ? ? ? ? ? C7 41"},
				{"RenderText candidate", "48 8B C4 48 89 58 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 ? ? ? ? 48 81 EC ? ? ? ? 0F 29 70 ? 0F 29 78 ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 4C 8B F2 48 89 54 ? ? 4C 8B E9"},
				{"UI render candidate", "48 89 5C ? ? 48 89 74 ? ? 57 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 84 ? ? ? ? ? 48 8B FA 48 8B D9 B9"},
				{"MoveInput tick candidate", "48 89 5C ? ? 55 56 57 41 56 41 57 48 8B EC 48 83 EC ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 ? 49 8B 01"},
				{"LocalPlayer camera candidate", "48 8B C4 48 89 70 ? 57 48 81 EC ? ? ? ? 0F 29 70 ? 0F 29 78"},
				{"HID key/mouse candidate", "48 89 5C ? ? 55 56 57 41 54 41 55 41 56 41 57 48 8B EC 48 81 EC ? ? ? ? ? ? 74 24 ? ? ? 7C 24 ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 ? 49 8B F8"}
			};

			scanSignatures(
				"1.21.130 bridge candidates",
				bridgeCandidates,
				sizeof(bridgeCandidates) / sizeof(bridgeCandidates[0]));

			// These signatures are from actively maintained 1.26.4x-era open-source
			// Bedrock tooling/client code. They are scan-only here: matching them does
			// not mean Horion's old object layouts or vtable indexes are safe yet.
			// The Platform_GameCore / ClientInstance / LocalPlayer / Player / Mob /
			// GameMode shapes come from Necromancer (Aug 2026). The packet and mouse
			// controls come from Spyglass, whose 1.26.40 payload explicitly covers
			// Windows 1.26.45.1.
			const SignatureCheck modern1264xCandidates[] = {
				{"Platform_GameCore global", "4C 89 3D ? ? ? ? 4D 85 FF"},
				{"ClientInstance vtable 1.26", "48 8D 05 ? ? ? ? 49 89 45 00 48 8D 05 ? ? ? ? 49 89 45 18 48 8D 05 ? ? ? ? 49 89 85 ? ? ? ? 48 8D 05 ? ? ? ? 49 89 85 ? ? ? ?"},
				{"LocalPlayer vtable 1.26", "48 8D 05 ? ? ? ? 48 89 07 48 8D 87 08 0F 00 00 48 89 85 ? ? ? ? C6 87 30 0F 00 00 00 C6 87 39 0F 00 00 00"},
				{"Player vtable 1.26", "48 8D 0D ? ? ? ? 49 89 0C 24 41 89 84 24 B8 0C 00 00 49 8D 84 24 C0 0C 00 00"},
				{"Mob vtable 1.26", "48 8D 05 ? ? ? ? 48 89 07 66 0F EF C0 F3 0F 7F 87 68 04 00 00 48 89 BD ? ? ? ? 48 C7 87 78 04 00 00 ? ? ? ?"},
				{"GameMode::attack 1.26", "55 41 57 41 56 41 54 56 57 53 48 81 EC ? ? ? ? 48 8D AC 24 ? ? ? ? 48 C7 85 ? ? ? ? ? ? ? ? 4C 89 CB 45 89 C6 49 89 D7 48 89 CF 48 8B 41 ? 48 8B 88 ? ? ? ? 48 85 C9"},
				{"GameMode::buildBlock 1.26", "55 41 57 41 56 41 55 41 54 56 57 53 48 81 EC ? ? ? ? 48 8D AC 24 ? ? ? ? 48 C7 85 ? ? ? ? ? ? ? ? 44 89 CB 44 89 C7 49 89 D6 48 89 CE 48 8B 41 ? 48 8B 80 ? ? ? ? 80 B8 ? ? ? ? ? 74 ?"},
				{"MinecraftPackets::createPacket", "56 48 83 EC 20 48 89 CE 81 FA ? 01 00 00 77 ? 89 D0 48 8D 0D ? ? ? ? 48 63 04 81 48 01 C8 FF E0 0F 57 C0 0F 11 06 48 89 F0 48 83 C4 20 5E"},
				{"MouseDevice::feed", "41 57 41 56 41 55 41 54 56 57 55 53 48 83 EC 48 44 89 CF 44 89 C3 89 D5 48 89 CE 44 0F B7 A4 24 C0 00 00 00 44 0F B7 AC 24 B8 00 00 00 44 0F B7 BC 24 B0 00 00 00 0F B6 84 24 C8 00 00 00"}
			};

			scanSignatures(
				"Bedrock 1.26.4x modern candidates",
				modern1264xCandidates,
				sizeof(modern1264xCandidates) / sizeof(modern1264xCandidates[0]));

			abortStartup("archived signatures are stale; modern 1.26.4x candidate results were logged");
		}

		logF("[compat] Critical archived signatures matched; entering archived GameData initialization");
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
