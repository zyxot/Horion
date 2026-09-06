#pragma once

#include "Modern126Overlay.h"
#include "Modern126PresentProbe.h"

// Minimal Bedrock 1.26.45.1 runtime bridge.
//
// The archived 1.18 hook table remains disabled. The validated 1.26 path keeps
// only the current runtime/object hooks plus the stable DXGI Present renderer.
namespace Modern126Runtime {
	inline bool modeSelected = false;
	inline bool hooksEnabled = false;
	inline int* keyMap = nullptr;
	inline void* minecraftGame = nullptr;
	inline C_ClientInstance* clientInstance = nullptr;
	inline C_LocalPlayer* localPlayer = nullptr;
	inline C_GameMode* gameMode = nullptr;
	inline C_GuiData* guiData = nullptr;
	inline C_LoopbackPacketSender* packetSender = nullptr;
	inline HMODULE dllModule = nullptr;
	inline std::unique_ptr<FuncHook> screenViewHook;
	inline std::unique_ptr<FuncHook> minecraftUpdateHook;
	inline std::unique_ptr<FuncHook> grabCursorHook;
	inline uintptr_t releaseCursorTarget = 0;
	inline bool cursorReleasedForOverlay = false;
	inline bool rawCursorOverrideActive = false;
	inline bool rawCursorPreviousGrabbed = true;
	inline bool loggedScreenHook = false;
	inline bool loggedUpdateHook = false;
	inline bool loggedDebugScreenLayer = false;
	inline bool loggedCursorBridge = false;
	inline bool loggedCursorUnavailable = false;
	inline bool loggedRawCursorFallback = false;

	inline uintptr_t resolveRipRelative(uintptr_t instruction, size_t displacementOffset = 3, size_t instructionLength = 7) {
		if (instruction == 0)
			return 0;
		const auto displacement = *reinterpret_cast<int32_t*>(instruction + displacementOffset);
		return instruction + instructionLength + displacement;
	}

	inline uintptr_t resolveRelativeCall(uintptr_t callInstruction) {
		return resolveRipRelative(callInstruction, 1, 5);
	}

	inline bool addressInMinecraft(uintptr_t address) {
		if (address == 0)
			return false;
		MEMORY_BASIC_INFORMATION info = {};
		const SIZE_T queried = VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info));
		return queried == sizeof(info) && info.Type == MEM_IMAGE &&
			info.AllocationBase == GetModuleHandleA("Minecraft.Windows.exe");
	}

	inline bool isDebugScreenViewUnsafe(void* view) {
		if (view == nullptr)
			return false;
		void* visualTree = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(view) + 0x48);
		if (visualTree == nullptr)
			return false;
		void* rootControl = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(visualTree) + 0x8);
		if (rootControl == nullptr)
			return false;
		auto* name = reinterpret_cast<std::string*>(reinterpret_cast<uintptr_t>(rootControl) + 0x20);
		return *name == "debug_screen";
	}

	inline bool isDebugScreenView(void* view) {
		__try {
			return isDebugScreenViewUnsafe(view);
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	inline C_LocalPlayer* refreshLocalPlayer() {
		if (clientInstance == nullptr)
			return nullptr;

		auto* vtable = *reinterpret_cast<uintptr_t**>(clientInstance);
		if (vtable == nullptr || !addressInMinecraft(vtable[0x1F]))
			return nullptr;

		using GetLocalPlayerFn = C_LocalPlayer*(__fastcall*)(void*);
		auto getLocalPlayer = reinterpret_cast<GetLocalPlayerFn>(vtable[0x1F]);
		localPlayer = getLocalPlayer(clientInstance);
		gameMode = localPlayer != nullptr
			? *reinterpret_cast<C_GameMode**>(reinterpret_cast<uintptr_t>(localPlayer) + 0xAA0)
			: nullptr;
		return localPlayer;
	}

	// Current clients try to re-grab the cursor during normal gameplay. While the
	// Horion UI is visible, suppress only that cursor-grab request so the pointer
	// remains available for the local UI. The original call is used normally when
	// the overlay is closed.
	inline void __fastcall grabCursorDetour(void* instance) {
		if (Modern126Overlay::visible)
			return;
		if (!grabCursorHook)
			return;
		auto original = grabCursorHook->GetFastcall<void, void*>();
		original(instance);
	}

	// The maintained current client keeps MinecraftGame::mouseGrabbed at +0x1D8.
	// These helpers are intentionally tiny POD/SEH functions so a bad read/write
	// fails closed instead of taking down the already-working renderer.
	inline bool readRawCursorGrabbed(bool* outValue) {
		if (outValue == nullptr || minecraftGame == nullptr)
			return false;
		__try {
			*outValue = *reinterpret_cast<bool*>(reinterpret_cast<uintptr_t>(minecraftGame) + 0x1D8);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	inline bool writeRawCursorGrabbed(bool value) {
		if (minecraftGame == nullptr)
			return false;
		__try {
			*reinterpret_cast<bool*>(reinterpret_cast<uintptr_t>(minecraftGame) + 0x1D8) = value;
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	inline void exposeWindowsCursor() {
		ClipCursor(nullptr);
		ReleaseCapture();
		SetCursor(LoadCursorW(nullptr, IDC_ARROW));
	}

	inline void restoreGameplayCursor() {
		if (rawCursorOverrideActive) {
			writeRawCursorGrabbed(rawCursorPreviousGrabbed);
			rawCursorOverrideActive = false;
			if (rawCursorPreviousGrabbed)
				SetCursor(nullptr);
			logF("[modern] Raw gameplay cursor state restored=%s", rawCursorPreviousGrabbed ? "GRABBED" : "FREE");
		}

		if (cursorReleasedForOverlay) {
			// Only re-grab in an active gameplay session. If the player disappeared
			// (for example, returning to the title screen), leave cursor ownership to
			// Minecraft instead of calling gameplay cursor code on stale state.
			if (clientInstance != nullptr && localPlayer != nullptr && grabCursorHook) {
				auto originalGrab = grabCursorHook->GetFastcall<void, void*>();
				originalGrab(clientInstance);
				logF("[modern] Gameplay cursor restored to Minecraft");
			}
			cursorReleasedForOverlay = false;
		}
	}

	inline void syncCursorForOverlay() {
		if (!Modern126Overlay::visible) {
			restoreGameplayCursor();
			return;
		}

		if (clientInstance == nullptr || localPlayer == nullptr || minecraftGame == nullptr)
			return;

		if (grabCursorHook && releaseCursorTarget != 0) {
			if (!cursorReleasedForOverlay) {
				using ReleaseCursorFn = void(__fastcall*)(void*);
				auto releaseCursor = reinterpret_cast<ReleaseCursorFn>(releaseCursorTarget);
				releaseCursor(clientInstance);
				cursorReleasedForOverlay = true;
				logF("[modern] Gameplay cursor released for Horion UI through ClientInstance");
			}
			exposeWindowsCursor();
			return;
		}

		// Exact 1.26.45.1 can fail the maintained grab/release signatures even
		// though the MinecraftGame object layout is already validated. In that
		// case, use the current mouseGrabbed field as a narrow compatibility fallback.
		if (!rawCursorOverrideActive) {
			bool previous = true;
			if (!readRawCursorGrabbed(&previous)) {
				if (!loggedCursorUnavailable) {
					loggedCursorUnavailable = true;
					logF("[modern] Raw gameplay cursor fallback failed to read MinecraftGame+0x1D8");
				}
				return;
			}
			rawCursorPreviousGrabbed = previous;
			rawCursorOverrideActive = true;
			if (!loggedRawCursorFallback) {
				loggedRawCursorFallback = true;
				logF("[modern] Raw gameplay cursor fallback ACTIVE; previous=%s", previous ? "GRABBED" : "FREE");
			}
		}

		if (!writeRawCursorGrabbed(false)) {
			logF("[modern] Raw gameplay cursor fallback write failed; restoring state");
			restoreGameplayCursor();
			return;
		}
		exposeWindowsCursor();
	}

	inline void __fastcall minecraftUpdateDetour(void* game) {
		auto original = minecraftUpdateHook->GetFastcall<void, void*>();
		original(game);
		refreshLocalPlayer();
		syncCursorForOverlay();
		if (!loggedUpdateHook) {
			loggedUpdateHook = true;
			logF("[modern] MinecraftGame::_update hook entered successfully");
			logF("[modern] Live LocalPlayer=%llX GameMode=%llX",
				reinterpret_cast<uintptr_t>(localPlayer), reinterpret_cast<uintptr_t>(gameMode));
		}
	}

	inline void __fastcall screenViewDetour(void* view, void* renderContext) {
		auto original = screenViewHook->GetFastcall<void, void*, void*>();
		original(view, renderContext);

		if (!loggedDebugScreenLayer && isDebugScreenView(view)) {
			loggedDebugScreenLayer = true;
			logF("[modern] debug_screen UI layer validated");
		}

		refreshLocalPlayer();
		g_Data.frameCount++;
		if (!loggedScreenHook) {
			loggedScreenHook = true;
			logF("[modern] ScreenView::setupAndRender hook entered successfully");
			logF("[modern] Render context=%llX GuiData=%llX",
				reinterpret_cast<uintptr_t>(renderContext), reinterpret_cast<uintptr_t>(guiData));
		}
	}

	inline void shutdownHooks() {
		if (!hooksEnabled)
			return;

		logF("[modern] Disabling 1.26 compatibility hooks");

		restoreGameplayCursor();
		if (grabCursorHook)
			grabCursorHook->enableHook(false);
		grabCursorHook.reset();
		releaseCursorTarget = 0;

		Modern126PresentProbe::shutdown();
		if (screenViewHook)
			screenViewHook->enableHook(false);
		if (minecraftUpdateHook)
			minecraftUpdateHook->enableHook(false);
		screenViewHook.reset();
		minecraftUpdateHook.reset();
		hooksEnabled = false;
	}

	inline DWORD WINAPI keyThread(LPVOID module) {
		logF("[modern] Key thread started; INSERT toggles test menu; CTRL+L unload is active");
		bool insertWasDown = false;
		while (isRunning && modeSelected) {
			if (keyMap != nullptr) {
				const bool insertDown = keyMap[VK_INSERT] != 0;
				if (insertDown && !insertWasDown)
					Modern126Overlay::toggle();
				insertWasDown = insertDown;

				const bool ctrl = keyMap[VK_CONTROL] != 0 || keyMap[VK_LCONTROL] != 0 || keyMap[VK_RCONTROL] != 0;
				if (ctrl && keyMap['L'] != 0) {
					logF("[modern] CTRL+L requested unload");
					isRunning = false;
					break;
				}
			}
			Sleep(5);
		}

		shutdownHooks();
		Sleep(100);
		FreeLibraryAndExitThread(static_cast<HMODULE>(module), 1);
	}

	inline bool tryStart(HMODULE module) {
		const uintptr_t platformSig = FindSignature("4C 89 3D ? ? ? ? 4D 85 FF");
		const uintptr_t clientVtableSig = FindSignature("48 8D 05 ? ? ? ? 49 89 45 00 48 8D 05 ? ? ? ? 49 89 45 18 48 8D 05 ? ? ? ? 49 89 85 ? ? ? ? 48 8D 05 ? ? ? ? 49 89 85 ? ? ? ?");
		const uintptr_t keyMapSig = FindSignature("48 8D 3D ? ? ? ? C7 04 B7");
		const uintptr_t screenViewCall = FindSignature("E8 ? ? ? ? 48 8B 4B ? 48 85 C9 74 ? 48 8B 01 48 8B 40 ? 48 89 FA FF 15 ? ? ? ? 48 8D 4D");
		const uintptr_t updateCall = FindSignature("E8 ? ? ? ? 48 8B 8F ? ? ? ? BA ? ? ? ? E8 ? ? ? ? 48 8B 9F");
		const uintptr_t attackBody = FindSignature("55 41 57 41 56 41 54 56 57 53 48 81 EC ? ? ? ? 48 8D AC 24 ? ? ? ? 48 C7 85 ? ? ? ? ? ? ? ? 4C 89 CB 45 89 C6 49 89 D7 48 89 CF 48 8B 41 ? 48 8B 88 ? ? ? ? 48 85 C9");
		const uintptr_t buildBody = FindSignature("55 41 57 41 56 41 55 41 54 56 57 53 48 81 EC ? ? ? ? 48 8D AC 24 ? ? ? ? 48 C7 85 ? ? ? ? ? ? ? ? 44 89 CB 44 89 C7 49 89 D6 48 89 CE 48 8B 41 ? 48 8B 80 ? ? ? ? 80 B8 ? ? ? ? ? 74 ?");

		if (platformSig == 0 || clientVtableSig == 0 || keyMapSig == 0 || screenViewCall == 0 ||
			updateCall == 0 || attackBody == 0 || buildBody == 0) {
			logF("[modern] Current 1.26 bridge signatures are incomplete; staying in diagnostic mode");
			return false;
		}

		const uintptr_t platformGlobal = resolveRipRelative(platformSig);
		void* winMain = platformGlobal != 0 ? *reinterpret_cast<void**>(platformGlobal) : nullptr;
		void* platformGameCore = winMain != nullptr
			? *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(winMain) + 0x8)
			: nullptr;
		minecraftGame = platformGameCore != nullptr
			? *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(platformGameCore) + 0x18)
			: nullptr;
		if (minecraftGame == nullptr)
			return false;

		using PrimaryClientMap = std::map<unsigned char, std::shared_ptr<C_ClientInstance>>;
		auto* clients = reinterpret_cast<PrimaryClientMap*>(reinterpret_cast<uintptr_t>(minecraftGame) + 0x938);
		auto primary = clients->find(0);
		if (primary == clients->end())
			return false;
		clientInstance = primary->second.get();
		if (clientInstance == nullptr)
			return false;

		const uintptr_t expectedClientVtable = resolveRipRelative(clientVtableSig);
		const uintptr_t actualClientVtable = *reinterpret_cast<uintptr_t*>(clientInstance);
		if (expectedClientVtable == 0 || actualClientVtable != expectedClientVtable)
			return false;

		void* ciMinecraftGame = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(clientInstance) + 0x1A0);
		guiData = *reinterpret_cast<C_GuiData**>(reinterpret_cast<uintptr_t>(clientInstance) + 0x648);
		packetSender = *reinterpret_cast<C_LoopbackPacketSender**>(reinterpret_cast<uintptr_t>(clientInstance) + 0x1C8);
		if (ciMinecraftGame != minecraftGame || guiData == nullptr || packetSender == nullptr)
			return false;

		refreshLocalPlayer();
		if (localPlayer != nullptr) {
			void* playerPacketSender = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(localPlayer) + 0x7F8);
			void* liveGameMode = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(localPlayer) + 0xAA0);
			void* gameModePlayer = liveGameMode != nullptr
				? *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(liveGameMode) + 0x8)
				: nullptr;
			if (playerPacketSender != packetSender || liveGameMode == nullptr || gameModePlayer != localPlayer)
				return false;
		}

		keyMap = reinterpret_cast<int*>(resolveRipRelative(keyMapSig));
		const uintptr_t screenViewTarget = resolveRelativeCall(screenViewCall);
		const uintptr_t updateTarget = resolveRelativeCall(updateCall);
		if (keyMap == nullptr || !addressInMinecraft(screenViewTarget) || !addressInMinecraft(updateTarget))
			return false;

		dllModule = module;
		modeSelected = true;
		logF("[modern] Bedrock 1.26.45.1 runtime bridge selected");
		logF("[modern] MinecraftGame=%llX ClientInstance=%llX LocalPlayer=%llX",
			reinterpret_cast<uintptr_t>(minecraftGame), reinterpret_cast<uintptr_t>(clientInstance), reinterpret_cast<uintptr_t>(localPlayer));
		logF("[modern] KeyMap=%llX ScreenView=%llX MinecraftGame::_update=%llX",
			reinterpret_cast<uintptr_t>(keyMap), screenViewTarget, updateTarget);

		screenViewHook = std::make_unique<FuncHook>(screenViewTarget, reinterpret_cast<void*>(screenViewDetour));
		minecraftUpdateHook = std::make_unique<FuncHook>(updateTarget, reinterpret_cast<void*>(minecraftUpdateDetour));
		screenViewHook->enableHook();
		minecraftUpdateHook->enableHook();
		hooksEnabled = true;

		// Cursor lifecycle support is optional. The current maintained signatures
		// are attempted first, but exact 1.26.45.1 builds can differ. If they do,
		// the raw MinecraftGame +0x1D8 compatibility path remains available.
		const uintptr_t grabCursorTarget = FindSignature("56 48 83 EC ? 48 89 CE 48 8B 01 48 8B 80 ? ? ? ? FF 15 ? ? ? ? 84 C0 74 ? 48 8B 8E ? ? ? ? 48 8B 01 48 8B 80 ? ? ? ? 48 8B 15 ? ? ? ? 48 83 C4 ? 5E 48 FF E2 90 48 83 C4 ? 5E C3 CC CC CC CC CC CC CC CC CC CC CC CC CC 56 48 83 EC");
		const uintptr_t releaseCursorCandidate = FindSignature("56 48 83 EC ? 48 89 CE 48 8B 01 48 8B 80 ? ? ? ? FF 15 ? ? ? ? 84 C0 74 ? 48 8B 8E ? ? ? ? 48 8B 01 48 8B 80 ? ? ? ? 48 8B 15 ? ? ? ? 48 83 C4 ? 5E 48 FF E2 90 48 83 C4 ? 5E C3 CC CC CC CC CC CC CC CC CC CC CC CC CC 56 53");
		if (addressInMinecraft(grabCursorTarget) && addressInMinecraft(releaseCursorCandidate) &&
			grabCursorTarget != releaseCursorCandidate) {
			releaseCursorTarget = releaseCursorCandidate;
			grabCursorHook = std::make_unique<FuncHook>(grabCursorTarget, reinterpret_cast<void*>(grabCursorDetour));
			grabCursorHook->enableHook();
			loggedCursorBridge = true;
			logF("[modern] Gameplay cursor bridge installed grab=%llX release=%llX",
				grabCursorTarget, releaseCursorTarget);
		} else {
			bool initialGrabbed = false;
			if (readRawCursorGrabbed(&initialGrabbed)) {
				logF("[modern] Gameplay cursor signatures unavailable; raw MinecraftGame+0x1D8 fallback armed initial=%s",
					initialGrabbed ? "GRABBED" : "FREE");
			} else if (!loggedCursorUnavailable) {
				loggedCursorUnavailable = true;
				logF("[modern] Gameplay cursor control unavailable; overlay remains render-only");
			}
		}

		Modern126PresentProbe::start();

		DWORD modernKeyThreadId = 0;
		CreateThread(nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(keyThread), module, 0, &modernKeyThreadId);
		logF("[modern] Pass-through hooks enabled; key thread id=%lu", modernKeyThreadId);
		logF("[modern] Archived Horion hook table intentionally skipped");
		return true;
	}

	inline bool isSelected() {
		return modeSelected;
	}
}
