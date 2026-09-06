#pragma once

#include "Modern126Overlay.h"

// Narrow current-Bedrock gameplay feature bridge. Keep gameplay mutations here
// isolated from the renderer so every feature can be validated independently.
namespace Modern126Gameplay {
	inline bool started = false;
	inline bool autoSprintEnabled = false;
	inline bool loggedAutoSprintReady = false;
	inline bool loggedAutoSprintFailure = false;
	inline uint64_t autoSprintCalls = 0;
	inline void* clientInstance = nullptr;
	inline int* keyMap = nullptr;

	inline bool addressInMinecraft(uintptr_t address) {
		if (address == 0)
			return false;
		MEMORY_BASIC_INFORMATION info = {};
		const SIZE_T queried = VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info));
		return queried == sizeof(info) && info.Type == MEM_IMAGE &&
			info.AllocationBase == GetModuleHandleA("Minecraft.Windows.exe");
	}

	inline void setAutoSprintEnabled(bool enabled) {
		if (autoSprintEnabled == enabled)
			return;
		autoSprintEnabled = enabled;
		logF("[modern] Movement/AutoSprint state=%s", enabled ? "ON" : "OFF");
	}

	inline void toggleAutoSprint() {
		setAutoSprintEnabled(!autoSprintEnabled);
	}

	inline void* refreshLocalPlayer() {
		if (clientInstance == nullptr)
			return nullptr;
		__try {
			auto* vtable = *reinterpret_cast<uintptr_t**>(clientInstance);
			if (vtable == nullptr || !addressInMinecraft(vtable[0x1F]))
				return nullptr;
			using GetLocalPlayerFn = void*(__fastcall*)(void*);
			auto getLocalPlayer = reinterpret_cast<GetLocalPlayerFn>(vtable[0x1F]);
			return getLocalPlayer(clientInstance);
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return nullptr;
		}
	}

	inline void tickAutoSprint(void* localPlayer) {
		if (!autoSprintEnabled || localPlayer == nullptr || keyMap == nullptr || Modern126Overlay::visible)
			return;

		// First gameplay canary uses the already-validated current KeyMap rather
		// than touching the newer ECS MoveInputComponent layout. Only force sprint
		// while the normal forward key is physically held.
		if (keyMap['W'] == 0)
			return;

		__try {
			auto* vtable = *reinterpret_cast<uintptr_t**>(localPlayer);
			if (vtable == nullptr || !addressInMinecraft(vtable[0x8B])) {
				if (!loggedAutoSprintFailure) {
					loggedAutoSprintFailure = true;
					logF("[modern] AutoSprint disabled: LocalPlayer vtable slot 0x8B failed validation");
				}
				autoSprintEnabled = false;
				return;
			}

			using SetSprintingFn = void(__fastcall*)(void*, bool);
			auto setSprinting = reinterpret_cast<SetSprintingFn>(vtable[0x8B]);
			setSprinting(localPlayer, true);
			++autoSprintCalls;

			if (!loggedAutoSprintReady) {
				loggedAutoSprintReady = true;
				logF("[modern] AutoSprint runtime call validated slot=0x8B target=%llX",
					vtable[0x8B]);
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			autoSprintEnabled = false;
			if (!loggedAutoSprintFailure) {
				loggedAutoSprintFailure = true;
				logF("[modern] AutoSprint disabled after guarded runtime exception");
			}
		}
	}

	inline DWORD WINAPI featureThread(LPVOID) {
		logF("[modern] Gameplay canary thread started; F6 toggles AutoSprint");
		bool f6WasDown = false;
		while (isRunning && started) {
			if (keyMap != nullptr) {
				const bool f6Down = keyMap[VK_F6] != 0;
				if (f6Down && !f6WasDown)
					toggleAutoSprint();
				f6WasDown = f6Down;
			}

			tickAutoSprint(refreshLocalPlayer());
			Sleep(5);
		}
		logF("[modern] Gameplay canary thread stopped");
		return 0;
	}

	inline bool start(void* liveClientInstance, int* liveKeyMap) {
		if (started)
			return true;
		if (liveClientInstance == nullptr || liveKeyMap == nullptr)
			return false;

		clientInstance = liveClientInstance;
		keyMap = liveKeyMap;
		started = true;

		DWORD threadId = 0;
		HANDLE thread = CreateThread(nullptr, 0, featureThread, nullptr, 0, &threadId);
		if (thread == nullptr) {
			started = false;
			clientInstance = nullptr;
			keyMap = nullptr;
			logF("[modern] Gameplay canary thread creation failed error=%lu", GetLastError());
			return false;
		}
		CloseHandle(thread);
		logF("[modern] Gameplay bridge armed; AutoSprint test key=F6 thread=%lu", threadId);
		return true;
	}

	inline void shutdown() {
		started = false;
		autoSprintEnabled = false;
		clientInstance = nullptr;
		keyMap = nullptr;
	}
}
