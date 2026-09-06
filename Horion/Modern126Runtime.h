#pragma once

// Minimal Bedrock 1.26.45.1 runtime bridge.
//
// This deliberately does NOT enable Horion's archived 1.18 hook table. The
// old hook initializer dereferences stale signatures and is the reason the
// unmodified client crashes on current Minecraft. Instead we first keep the
// DLL alive with two current, pass-through hooks and the current KeyMap. Once
// this bridge is proven stable, the old Horion systems can be ported onto it
// one subsystem at a time.
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
	inline bool loggedScreenHook = false;
	inline bool loggedUpdateHook = false;
	inline bool loggedRenderCanary = false;
	inline bool renderCanaryFailed = false;

	// Current MinecraftUIRenderContext uses these simple POD layouts. Keeping
	// them local to the compatibility bridge avoids pulling the archived 1.18
	// rendering SDK into the first live 1.26 render test.
	struct ModernRectangleArea {
		float left;
		float right;
		float top;
		float bottom;
	};

	struct ModernColor {
		float r;
		float g;
		float b;
		float a;
	};

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

	inline void drawRenderCanary(void* renderContext) {
		if (renderContext == nullptr || renderCanaryFailed)
			return;

		// 1.26 MinecraftUIRenderContext slot 0xF is fillRectangle. Use only POD
		// arguments and guard the call with SEH so a future game update cannot turn
		// this visual probe into another startup crash.
		__try {
			auto* renderVtable = *reinterpret_cast<uintptr_t**>(renderContext);
			if (renderVtable == nullptr || !addressInMinecraft(renderVtable[0xF])) {
				renderCanaryFailed = true;
				logF("[modern] Render canary disabled: MinecraftUIRenderContext slot 0xF is invalid");
				return;
			}

			const ModernRectangleArea rect { 8.f, 168.f, 8.f, 30.f };
			const ModernColor color { 0.10f, 0.65f, 1.00f, 0.85f };
			using FillRectangleFn = void(__fastcall*)(void*, const ModernRectangleArea&, const ModernColor&, float);
			auto fillRectangle = reinterpret_cast<FillRectangleFn>(renderVtable[0xF]);
			fillRectangle(renderContext, rect, color, 1.0f);

			if (!loggedRenderCanary) {
				loggedRenderCanary = true;
				logF("[modern] 1.26 fillRectangle render canary executed successfully");
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			renderCanaryFailed = true;
			logF("[modern] Render canary trapped SEH exception 0x%08X and was disabled", GetExceptionCode());
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

	inline void __fastcall minecraftUpdateDetour(void* game) {
		auto original = minecraftUpdateHook->GetFastcall<void, void*>();
		original(game);
		refreshLocalPlayer();
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
		drawRenderCanary(renderContext);
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
		if (screenViewHook)
			screenViewHook->enableHook(false);
		if (minecraftUpdateHook)
			minecraftUpdateHook->enableHook(false);
		screenViewHook.reset();
		minecraftUpdateHook.reset();
		hooksEnabled = false;
	}

	inline DWORD WINAPI keyThread(LPVOID module) {
		logF("[modern] Key thread started; CTRL+L unload is active");
		while (isRunning && modeSelected) {
			if (keyMap != nullptr) {
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
		// Only activate this bridge when the signatures verified on the user's
		// Minecraft.Windows.exe 1.26.45.1 are all present. Otherwise the archived
		// diagnostic path remains in control and fails closed.
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
