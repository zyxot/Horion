#pragma once

// Narrow current-Bedrock gameplay feature bridge. Keep gameplay mutations here
// isolated from the renderer so every feature can be validated independently.
namespace Modern126Gameplay {
	inline bool autoSprintEnabled = false;
	inline bool loggedAutoSprintReady = false;
	inline bool loggedAutoSprintFailure = false;
	inline uint64_t autoSprintCalls = 0;

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

	inline void tickAutoSprint(void* localPlayer, int* keyMap, bool menuVisible) {
		if (!autoSprintEnabled || localPlayer == nullptr || keyMap == nullptr || menuVisible)
			return;

		// First canary uses the already-validated current KeyMap rather than touching
		// the newer ECS MoveInputComponent layout. Only force sprint while W is held.
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
}
