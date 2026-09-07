#pragma once

#include "Modern126Gameplay.h"
#include "Modern126Overlay.h"
#include "Modern126Visuals.h"
#include <d2d1_1.h>
#include <dwrite.h>
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <string>

// Modern 1.26 test batch for selected legacy Horion features.
//
// The archived implementations are not linked directly: Aimbot and Zoom are
// rebuilt on the validated local 1.26 component/projection paths; Help is local
// UI; Give routes through Minecraft's normal chat command parser so permissions
// remain authoritative; Godmode is deliberately a local fall-protection canary
// and does not recreate the archived movement-packet onGround spoof.
namespace Modern126FeatureBatch {
	inline bool aimbotEnabled = false;
	inline bool zoomEnabled = false;
	inline bool godmodeEnabled = false;
	inline bool helpVisible = false;
	inline bool giveRequested = false;
	inline bool menuLeftWasDown = false;

	inline bool f8WasDown = false;
	inline bool f9WasDown = false;
	inline bool f10WasDown = false;
	inline bool f11WasDown = false;
	inline bool f12WasDown = false;
	inline ULONGLONG lastAimTick = 0;
	inline bool loggedAimbotReady = false;
	inline bool loggedZoomReady = false;
	inline bool loggedGodmodeReady = false;
	inline bool loggedGiveReady = false;

	inline bool zoomApplied = false;
	inline Modern126Visuals::Mat4Lite zoomBase = {};
	inline Modern126Visuals::Mat4Lite zoomWritten = {};

	inline bool anyEnabled() {
		return aimbotEnabled || zoomEnabled || godmodeEnabled || helpVisible;
	}

	inline float normalizeAngle(float angle) {
		while (angle > 180.0f)
			angle -= 360.0f;
		while (angle < -180.0f)
			angle += 360.0f;
		return angle;
	}

	inline float clampFloat(float value, float low, float high) {
		return value < low ? low : (value > high ? high : value);
	}

	inline bool pointInside(const POINT& point, const D2D1_RECT_F& rect) {
		return static_cast<float>(point.x) >= rect.left && static_cast<float>(point.x) <= rect.right &&
			static_cast<float>(point.y) >= rect.top && static_cast<float>(point.y) <= rect.bottom;
	}

	inline void setAimbot(bool enabled) {
		if (aimbotEnabled == enabled)
			return;
		aimbotEnabled = enabled;
		logF("[modern] Combat/Aimbot state=%s local-rotation path", enabled ? "ON" : "OFF");
	}

	inline void setZoom(bool enabled) {
		if (zoomEnabled == enabled)
			return;
		zoomEnabled = enabled;
		logF("[modern] Visuals/Zoom state=%s; hold C while enabled", enabled ? "ON" : "OFF");
	}

	inline void setGodmode(bool enabled) {
		if (godmodeEnabled == enabled)
			return;
		godmodeEnabled = enabled;
		logF("[modern] Player/Godmode(Local) state=%s; packet spoofing intentionally absent", enabled ? "ON" : "OFF");
	}

	inline bool getGameRenderer(void** outRenderer) {
		if (outRenderer == nullptr)
			return false;
		*outRenderer = nullptr;
		void* minecraftGame = Modern126Visuals::resolveMinecraftGame();
		if (minecraftGame == nullptr)
			return false;
		void* renderer = nullptr;
		if (!Modern126Visuals::safeRead(reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(minecraftGame) + 0x1318), &renderer) || renderer == nullptr)
			return false;
		*outRenderer = renderer;
		return true;
	}

	inline bool matrixZoomCellsMatch(const Modern126Visuals::Mat4Lite& a, const Modern126Visuals::Mat4Lite& b) {
		return std::fabs(a.m[0] - b.m[0]) < 0.0005f && std::fabs(a.m[5] - b.m[5]) < 0.0005f;
	}

	inline void restoreZoom() {
		if (!zoomApplied)
			return;
		void* renderer = nullptr;
		if (getGameRenderer(&renderer)) {
			auto* projection = reinterpret_cast<Modern126Visuals::Mat4Lite*>(reinterpret_cast<uintptr_t>(renderer) + 0x400);
			Modern126Visuals::Mat4Lite current = {};
			if (Modern126Visuals::safeRead(projection, &current) && matrixZoomCellsMatch(current, zoomWritten) &&
				Modern126Gameplay::writableObject(projection, sizeof(Modern126Visuals::Mat4Lite))) {
				__try {
					*projection = zoomBase;
				}
				__except (EXCEPTION_EXECUTE_HANDLER) {
				}
			}
		}
		zoomApplied = false;
	}

	inline void tickZoom() {
		const bool zoomHeld = zoomEnabled && !Modern126Overlay::visible && (GetAsyncKeyState('C') & 0x8000) != 0;
		if (!zoomHeld) {
			restoreZoom();
			return;
		}

		void* renderer = nullptr;
		if (!getGameRenderer(&renderer))
			return;
		auto* projection = reinterpret_cast<Modern126Visuals::Mat4Lite*>(reinterpret_cast<uintptr_t>(renderer) + 0x400);
		if (!Modern126Gameplay::writableObject(projection, sizeof(Modern126Visuals::Mat4Lite)))
			return;

		Modern126Visuals::Mat4Lite current = {};
		if (!Modern126Visuals::safeRead(projection, &current) || !Modern126Visuals::finiteMatrix(current))
			return;

		// Minecraft may rebuild its projection every frame. If the cells still
		// match what we wrote, leave them alone. Otherwise treat the current matrix
		// as a fresh native projection and apply the zoom exactly once.
		if (zoomApplied && matrixZoomCellsMatch(current, zoomWritten))
			return;

		zoomBase = current;
		zoomWritten = current;
		constexpr float zoomFactor = 2.35f;
		zoomWritten.m[0] *= zoomFactor;
		zoomWritten.m[5] *= zoomFactor;
		__try {
			*projection = zoomWritten;
			zoomApplied = true;
			if (!loggedZoomReady) {
				loggedZoomReady = true;
				logF("[modern] Zoom projection bridge READY factor=%.2f key=C", zoomFactor);
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			zoomApplied = false;
			zoomEnabled = false;
			logF("[modern] Zoom disabled after guarded projection write failure");
		}
	}

	inline void tickGodmode() {
		if (!godmodeEnabled || Modern126Overlay::visible)
			return;
		void* localPlayer = Modern126Gameplay::refreshLocalPlayer();
		if (localPlayer == nullptr)
			return;
		Modern126Gameplay::StateVectorLite* state = nullptr;
		Modern126Gameplay::ActorRotationLite* rotation = nullptr;
		if (!Modern126Gameplay::resolveFlyComponents(localPlayer, &state, &rotation) || state == nullptr)
			return;

		__try {
			// Local-only first port: prevent high downward velocity from accumulating.
			// This is useful for offline fall-damage testing but intentionally does
			// not claim invulnerability against server-authoritative damage.
			if (state->velocity.y < -0.62f)
				state->velocity.y = -0.18f;
			if (!loggedGodmodeReady) {
				loggedGodmodeReady = true;
				logF("[modern] Godmode(Local) fall-protection bridge READY; server damage authority unchanged");
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			godmodeEnabled = false;
			logF("[modern] Godmode(Local) disabled after guarded StateVector write failure");
		}
	}

	inline void tickAimbot(bool entitySnapshotReady) {
		if (!aimbotEnabled || !entitySnapshotReady || Modern126Overlay::visible || (GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0)
			return;
		const ULONGLONG now = GetTickCount64();
		if (now - lastAimTick < 16)
			return;
		lastAimTick = now;

		void* localPlayer = Modern126Gameplay::refreshLocalPlayer();
		if (localPlayer == nullptr)
			return;
		Modern126Gameplay::StateVectorLite* state = nullptr;
		Modern126Gameplay::ActorRotationLite* rotation = nullptr;
		if (!Modern126Gameplay::resolveFlyComponents(localPlayer, &state, &rotation) || state == nullptr || rotation == nullptr)
			return;

		const float eyeX = state->pos.x;
		const float eyeY = state->pos.y + 1.62f;
		const float eyeZ = state->pos.z;
		float bestScore = 1.0e9f;
		float bestYawDelta = 0.0f;
		float bestPitchDelta = 0.0f;
		bool found = false;
		constexpr float maxRangeSq = 12.0f * 12.0f;
		constexpr float maxYaw = 42.0f;
		constexpr float maxPitch = 32.0f;
		constexpr size_t maxCandidates = 96;

		const size_t count = std::min(Modern126Visuals::entityBoxes.size(), maxCandidates);
		for (size_t i = 0; i < count; ++i) {
			const auto& box = Modern126Visuals::entityBoxes[i];
			const float targetX = (box.lower.x + box.higher.x) * 0.5f;
			const float targetY = box.higher.y - 0.18f;
			const float targetZ = (box.lower.z + box.higher.z) * 0.5f;
			const float dx = targetX - eyeX;
			const float dy = targetY - eyeY;
			const float dz = targetZ - eyeZ;
			const float horizontal = std::sqrt(dx * dx + dz * dz);
			const float distanceSq = dx * dx + dy * dy + dz * dz;
			if (distanceSq < 0.25f || distanceSq > maxRangeSq || horizontal < 0.01f)
				continue;

			const float desiredYaw = std::atan2(-dx, dz) * 57.2957795131f;
			const float desiredPitch = -std::atan2(dy, horizontal) * 57.2957795131f;
			const float yawDelta = normalizeAngle(desiredYaw - rotation->rotation.y);
			const float pitchDelta = normalizeAngle(desiredPitch - rotation->rotation.x);
			if (std::fabs(yawDelta) > maxYaw || std::fabs(pitchDelta) > maxPitch)
				continue;
			const float score = yawDelta * yawDelta + pitchDelta * pitchDelta;
			if (score < bestScore) {
				bestScore = score;
				bestYawDelta = yawDelta;
				bestPitchDelta = pitchDelta;
				found = true;
			}
		}

		if (!found)
			return;
		constexpr float smoothing = 0.28f;
		__try {
			rotation->rotation.y = normalizeAngle(rotation->rotation.y + bestYawDelta * smoothing);
			rotation->rotation.x = clampFloat(rotation->rotation.x + bestPitchDelta * smoothing, -89.0f, 89.0f);
			if (!loggedAimbotReady) {
				loggedAimbotReady = true;
				logF("[modern] Aimbot local rotation bridge READY range=12 fov=42/32 hold=LMB smoothing=0.28");
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			aimbotEnabled = false;
			logF("[modern] Aimbot disabled after guarded ActorRotation write failure");
		}
	}

	inline bool sendKey(WORD key) {
		INPUT input[2] = {};
		input[0].type = INPUT_KEYBOARD;
		input[0].ki.wVk = key;
		input[1] = input[0];
		input[1].ki.dwFlags = KEYEVENTF_KEYUP;
		return SendInput(2, input, sizeof(INPUT)) == 2;
	}

	inline bool sendUnicode(const std::wstring& text) {
		for (wchar_t ch : text) {
			INPUT input[2] = {};
			input[0].type = INPUT_KEYBOARD;
			input[0].ki.wScan = ch;
			input[0].ki.dwFlags = KEYEVENTF_UNICODE;
			input[1] = input[0];
			input[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
			if (SendInput(2, input, sizeof(INPUT)) != 2)
				return false;
		}
		return true;
	}

	inline void executeGiveRequest() {
		if (!giveRequested)
			return;
		giveRequested = false;

		HWND foreground = GetForegroundWindow();
		DWORD foregroundPid = 0;
		if (foreground == nullptr || GetWindowThreadProcessId(foreground, &foregroundPid) == 0 || foregroundPid != GetCurrentProcessId()) {
			logF("[modern] GiveCommand skipped: Minecraft is not the foreground window");
			return;
		}

		// Route through the game's normal command surface. This deliberately leaves
		// cheats/operator/server permission checks intact instead of reviving the
		// archived direct inventory-transaction implementation.
		Modern126Overlay::visible = false;
		Sleep(45);
		if (!sendKey('T')) {
			logF("[modern] GiveCommand failed to open chat (default T binding expected)");
			return;
		}
		Sleep(70);
		const std::wstring command = L"/give @s diamond 1";
		if (!sendUnicode(command)) {
			logF("[modern] GiveCommand failed while typing authorized command text");
			return;
		}
		Sleep(20);
		sendKey(VK_RETURN);
		if (!loggedGiveReady) {
			loggedGiveReady = true;
			logF("[modern] GiveCommand authorized chat route READY default='/give @s diamond 1'");
		}
	}

	inline void processHotkeys() {
		auto edge = [](int key, bool& wasDown) {
			const bool down = (GetAsyncKeyState(key) & 0x8000) != 0;
			const bool pressed = down && !wasDown;
			wasDown = down;
			return pressed;
		};

		if (edge(VK_F8, f8WasDown))
			setAimbot(!aimbotEnabled);
		if (edge(VK_F9, f9WasDown))
			setZoom(!zoomEnabled);
		if (edge(VK_F10, f10WasDown))
			setGodmode(!godmodeEnabled);
		if (edge(VK_F11, f11WasDown)) {
			helpVisible = !helpVisible;
			logF("[modern] HelpCommand panel=%s", helpVisible ? "OPEN" : "CLOSED");
		}
		if (edge(VK_F12, f12WasDown))
			giveRequested = true;
	}

	inline void tickFrame(bool entitySnapshotReady) {
		processHotkeys();
		tickAimbot(entitySnapshotReady);
		tickZoom();
		tickGodmode();
		executeGiveRequest();
	}

	inline void renderHelp(ID2D1DeviceContext* context, ID2D1Bitmap1* target,
		IDWriteTextFormat* titleFormat, IDWriteTextFormat* bodyFormat,
		ID2D1SolidColorBrush* panelBrush, ID2D1SolidColorBrush* headerBrush, ID2D1SolidColorBrush* textBrush) {
		if (!helpVisible || context == nullptr || target == nullptr || titleFormat == nullptr || bodyFormat == nullptr ||
			panelBrush == nullptr || headerBrush == nullptr || textBrush == nullptr)
			return;

		const D2D1_SIZE_F size = target->GetSize();
		const float left = size.width > 560.0f ? size.width - 535.0f : 12.0f;
		const float top = 92.0f;
		const D2D1_RECT_F panel = { left, top, left + 510.0f, top + 250.0f };
		const D2D1_RECT_F header = { left, top, left + 510.0f, top + 36.0f };
		context->FillRectangle(panel, panelBrush);
		context->FillRectangle(header, headerBrush);
		static const wchar_t title[] = L"HORION 1.26 HELP";
		context->DrawText(title, _countof(title) - 1, titleFormat,
			D2D1::RectF(left + 12.0f, top + 5.0f, left + 495.0f, top + 33.0f), textBrush);

		static const wchar_t* lines[] = {
			L"F8  Aimbot toggle  | hold LMB to aim",
			L"F9  Zoom toggle    | hold C to zoom",
			L"F10 Godmode(Local) | fall-protection test",
			L"F11 Help panel     | show/hide this panel",
			L"F12 GiveCommand    | /give @s diamond 1",
			L"Give uses Minecraft permissions; no inventory exploit.",
			L"Godmode does not spoof movement packets or server state."
		};
		float y = top + 46.0f;
		for (const wchar_t* line : lines) {
			context->DrawText(line, static_cast<UINT32>(wcslen(line)), bodyFormat,
				D2D1::RectF(left + 14.0f, y, left + 496.0f, y + 26.0f), textBrush);
			y += 27.0f;
		}
	}

	inline void drawMenu(ID2D1DeviceContext* context, IDWriteTextFormat* titleFormat, IDWriteTextFormat* bodyFormat,
		ID2D1SolidColorBrush* panelBrush, ID2D1SolidColorBrush* headerBrush,
		ID2D1SolidColorBrush* hoverBrush, ID2D1SolidColorBrush* activeBrush, ID2D1SolidColorBrush* textBrush,
		const POINT& mouse, bool mouseValid, bool menuVisible) {
		if (!menuVisible || context == nullptr || titleFormat == nullptr || bodyFormat == nullptr) {
			menuLeftWasDown = false;
			return;
		}

		const D2D1_RECT_F panel = { 24.0f, 552.0f, 860.0f, 690.0f };
		const D2D1_RECT_F header = { 24.0f, 552.0f, 860.0f, 586.0f };
		context->FillRectangle(panel, panelBrush);
		context->FillRectangle(header, headerBrush);
		static const wchar_t title[] = L"LEGACY PORT TEST BATCH";
		context->DrawText(title, _countof(title) - 1, titleFormat,
			D2D1::RectF(36.0f, 557.0f, 846.0f, 584.0f), textBrush);

		const D2D1_RECT_F aim = { 38.0f, 610.0f, 190.0f, 653.0f };
		const D2D1_RECT_F zoom = { 198.0f, 610.0f, 350.0f, 653.0f };
		const D2D1_RECT_F god = { 358.0f, 610.0f, 510.0f, 653.0f };
		const D2D1_RECT_F help = { 518.0f, 610.0f, 670.0f, 653.0f };
		const D2D1_RECT_F give = { 678.0f, 610.0f, 846.0f, 653.0f };

		const bool hoverAim = mouseValid && pointInside(mouse, aim);
		const bool hoverZoom = mouseValid && pointInside(mouse, zoom);
		const bool hoverGod = mouseValid && pointInside(mouse, god);
		const bool hoverHelp = mouseValid && pointInside(mouse, help);
		const bool hoverGive = mouseValid && pointInside(mouse, give);
		const bool leftDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
		if (leftDown && !menuLeftWasDown) {
			if (hoverAim)
				setAimbot(!aimbotEnabled);
			else if (hoverZoom)
				setZoom(!zoomEnabled);
			else if (hoverGod)
				setGodmode(!godmodeEnabled);
			else if (hoverHelp) {
				helpVisible = !helpVisible;
				logF("[modern] HelpCommand panel=%s", helpVisible ? "OPEN" : "CLOSED");
			} else if (hoverGive)
				giveRequested = true;
		}
		menuLeftWasDown = leftDown;

		auto drawToggle = [&](const D2D1_RECT_F& rect, bool enabled, bool hovered, const wchar_t* onText, const wchar_t* offText) {
			ID2D1SolidColorBrush* brush = enabled ? activeBrush : (hovered ? hoverBrush : headerBrush);
			context->FillRectangle(rect, brush);
			const wchar_t* text = enabled ? onText : offText;
			context->DrawText(text, static_cast<UINT32>(wcslen(text)), bodyFormat,
				D2D1::RectF(rect.left + 8.0f, rect.top + 8.0f, rect.right - 4.0f, rect.bottom - 4.0f), textBrush);
		};

		drawToggle(aim, aimbotEnabled, hoverAim, L"AIMBOT: ON", L"AIMBOT: OFF");
		drawToggle(zoom, zoomEnabled, hoverZoom, L"ZOOM: ON", L"ZOOM: OFF");
		drawToggle(god, godmodeEnabled, hoverGod, L"GODMODE: ON", L"GODMODE: OFF");
		drawToggle(help, helpVisible, hoverHelp, L"HELP: OPEN", L"HELP: CLOSED");
		ID2D1SolidColorBrush* giveBrush = hoverGive ? hoverBrush : headerBrush;
		context->FillRectangle(give, giveBrush);
		static const wchar_t giveText[] = L"GIVE DIAMOND";
		context->DrawText(giveText, _countof(giveText) - 1, bodyFormat,
			D2D1::RectF(give.left + 8.0f, give.top + 8.0f, give.right - 4.0f, give.bottom - 4.0f), textBrush);
	}

	inline void shutdown() {
		restoreZoom();
		aimbotEnabled = false;
		zoomEnabled = false;
		godmodeEnabled = false;
		helpVisible = false;
		giveRequested = false;
		menuLeftWasDown = false;
		f8WasDown = f9WasDown = f10WasDown = f11WasDown = f12WasDown = false;
	}
}
