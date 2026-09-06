#pragma once

#include "Modern126Overlay.h"
#include <cmath>

// Narrow current-Bedrock gameplay feature bridge. Keep gameplay mutations here
// isolated from the renderer so every feature can be validated independently.
namespace Modern126Gameplay {
	inline bool started = false;
	inline bool autoSprintEnabled = false;
	inline bool flyEnabled = false;
	inline bool loggedAutoSprintReady = false;
	inline bool loggedAutoSprintFailure = false;
	inline bool loggedFlyReady = false;
	inline bool loggedFlyFailure = false;
	inline uint64_t autoSprintCalls = 0;
	inline uint64_t flyTicks = 0;
	inline void* clientInstance = nullptr;
	inline int* keyMap = nullptr;

	struct Vec2Lite {
		float x;
		float y;
	};

	struct Vec3Lite {
		float x;
		float y;
		float z;
	};

	// Current 1.26 actor component layout used by the maintained client:
	// LocalPlayer +0x218 -> StateVectorComponent { pos, posOld, velocity }
	// LocalPlayer +0x228 -> ActorRotationComponent { rotation, rotationOld }
	struct StateVectorLite {
		Vec3Lite pos;
		Vec3Lite posOld;
		Vec3Lite velocity;
	};

	struct ActorRotationLite {
		Vec2Lite rotation;
		Vec2Lite rotationOld;
	};

	inline bool addressInMinecraft(uintptr_t address) {
		if (address == 0)
			return false;
		MEMORY_BASIC_INFORMATION info = {};
		const SIZE_T queried = VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info));
		return queried == sizeof(info) && info.Type == MEM_IMAGE &&
			info.AllocationBase == GetModuleHandleA("Minecraft.Windows.exe");
	}

	inline bool writableObject(const void* address, size_t requiredBytes) {
		if (address == nullptr || requiredBytes == 0)
			return false;
		MEMORY_BASIC_INFORMATION info = {};
		if (VirtualQuery(address, &info, sizeof(info)) != sizeof(info) || info.State != MEM_COMMIT)
			return false;
		if ((info.Protect & PAGE_GUARD) != 0 || (info.Protect & PAGE_NOACCESS) != 0)
			return false;
		const uintptr_t begin = reinterpret_cast<uintptr_t>(address);
		const uintptr_t end = begin + requiredBytes;
		const uintptr_t regionEnd = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
		return end >= begin && end <= regionEnd;
	}

	inline bool isAutoSprintEnabled() {
		return autoSprintEnabled;
	}

	inline bool isFlyEnabled() {
		return flyEnabled;
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

	inline bool resolveFlyComponents(void* localPlayer, StateVectorLite** stateOut, ActorRotationLite** rotationOut) {
		if (stateOut == nullptr || rotationOut == nullptr || localPlayer == nullptr)
			return false;
		*stateOut = nullptr;
		*rotationOut = nullptr;

		__try {
			auto* state = *reinterpret_cast<StateVectorLite**>(reinterpret_cast<uintptr_t>(localPlayer) + 0x218);
			auto* rotation = *reinterpret_cast<ActorRotationLite**>(reinterpret_cast<uintptr_t>(localPlayer) + 0x228);
			if (!writableObject(state, sizeof(StateVectorLite)) || !writableObject(rotation, sizeof(ActorRotationLite)))
				return false;

			if (!std::isfinite(state->pos.x) || !std::isfinite(state->pos.y) || !std::isfinite(state->pos.z) ||
				!std::isfinite(state->velocity.x) || !std::isfinite(state->velocity.y) || !std::isfinite(state->velocity.z) ||
				!std::isfinite(rotation->rotation.x) || !std::isfinite(rotation->rotation.y))
				return false;
			if (std::fabs(rotation->rotation.y) > 10000.0f)
				return false;

			*stateOut = state;
			*rotationOut = rotation;
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	inline void releaseFlyVelocity() {
		void* localPlayer = refreshLocalPlayer();
		if (localPlayer == nullptr)
			return;
		StateVectorLite* state = nullptr;
		ActorRotationLite* rotation = nullptr;
		if (!resolveFlyComponents(localPlayer, &state, &rotation) || state == nullptr)
			return;
		__try {
			state->velocity = { 0.0f, 0.0f, 0.0f };
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
		}
	}

	inline void setFlyEnabled(bool enabled) {
		if (flyEnabled == enabled)
			return;
		if (!enabled)
			releaseFlyVelocity();
		flyEnabled = enabled;
		loggedFlyFailure = false;
		if (enabled)
			logF("[modern] Movement/Fly state=ON; close the INSERT menu to control flight");
		else
			logF("[modern] Movement/Fly state=OFF");
	}

	inline void toggleFly() {
		setFlyEnabled(!flyEnabled);
	}

	inline void tickAutoSprint(void* localPlayer) {
		if (!autoSprintEnabled || localPlayer == nullptr || keyMap == nullptr || Modern126Overlay::visible)
			return;
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
				logF("[modern] AutoSprint runtime call validated slot=0x8B target=%llX", vtable[0x8B]);
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

	inline void tickFly(void* localPlayer) {
		if (!flyEnabled || localPlayer == nullptr || keyMap == nullptr || Modern126Overlay::visible)
			return;

		StateVectorLite* state = nullptr;
		ActorRotationLite* rotation = nullptr;
		if (!resolveFlyComponents(localPlayer, &state, &rotation)) {
			flyEnabled = false;
			if (!loggedFlyFailure) {
				loggedFlyFailure = true;
				logF("[modern] Fly disabled: current StateVector/ActorRotation layout failed validation");
			}
			return;
		}

		__try {
			float forward = 0.0f;
			float strafe = 0.0f;
			float vertical = 0.0f;
			if (keyMap['W'] != 0) forward += 1.0f;
			if (keyMap['S'] != 0) forward -= 1.0f;
			if (keyMap['D'] != 0) strafe += 1.0f;
			if (keyMap['A'] != 0) strafe -= 1.0f;
			if (keyMap[VK_SPACE] != 0) vertical += 1.0f;
			if (keyMap[VK_SHIFT] != 0 || keyMap[VK_LSHIFT] != 0 || keyMap[VK_RSHIFT] != 0) vertical -= 1.0f;

			const float yaw = (rotation->rotation.y + 90.0f) * (3.14159265358979323846f / 180.0f);
			const float c = std::cos(yaw);
			const float s = std::sin(yaw);
			float wishX = (forward * c) - (strafe * s);
			float wishZ = (forward * s) + (strafe * c);
			const float horizontal = std::sqrt((wishX * wishX) + (wishZ * wishZ));
			if (horizontal > 1.0f) {
				wishX /= horizontal;
				wishZ /= horizontal;
			}

			// Apply after MinecraftGame::_update so normal physics does not immediately
			// overwrite the canary velocity. This remains local/offline compatibility
			// work: no packet spoofing or server-correction bypass is performed.
			constexpr float flySpeed = 0.45f;
			state->velocity.x = wishX * flySpeed;
			state->velocity.y = vertical * flySpeed;
			state->velocity.z = wishZ * flySpeed;
			++flyTicks;

			if (!loggedFlyReady) {
				loggedFlyReady = true;
				logF("[modern] Fly runtime component bridge validated StateVector=%llX Rotation=%llX speed=%.2f",
					reinterpret_cast<uintptr_t>(state), reinterpret_cast<uintptr_t>(rotation), flySpeed);
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			flyEnabled = false;
			if (!loggedFlyFailure) {
				loggedFlyFailure = true;
				logF("[modern] Fly disabled after guarded runtime exception");
			}
		}
	}

	// Called from the validated MinecraftGame::_update detour, after the original
	// game update. Keeping gameplay writes synchronized to that hook makes motion
	// deterministic compared with racing Minecraft from the hotkey thread.
	inline void tickFromGameUpdate(void* localPlayer) {
		if (!started)
			return;
		tickAutoSprint(localPlayer);
		tickFly(localPlayer);
	}

	inline DWORD WINAPI featureThread(LPVOID) {
		logF("[modern] Gameplay hotkey thread started; F6=AutoSprint F7=Fly");
		bool f6WasDown = false;
		bool f7WasDown = false;
		while (isRunning && started) {
			if (keyMap != nullptr) {
				const bool f6Down = keyMap[VK_F6] != 0;
				if (f6Down && !f6WasDown)
					toggleAutoSprint();
				f6WasDown = f6Down;

				const bool f7Down = keyMap[VK_F7] != 0;
				if (f7Down && !f7WasDown)
					toggleFly();
				f7WasDown = f7Down;
			}
			Sleep(5);
		}
		logF("[modern] Gameplay hotkey thread stopped");
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
			logF("[modern] Gameplay hotkey thread creation failed error=%lu", GetLastError());
			return false;
		}
		CloseHandle(thread);
		logF("[modern] Gameplay bridge armed; GUI + F6 AutoSprint + F7 Fly thread=%lu", threadId);
		return true;
	}

	inline void shutdown() {
		if (flyEnabled)
			releaseFlyVelocity();
		started = false;
		autoSprintEnabled = false;
		flyEnabled = false;
		clientInstance = nullptr;
		keyMap = nullptr;
	}
}
