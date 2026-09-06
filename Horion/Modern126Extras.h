#pragma once

#include "Modern126Gameplay.h"
#include "Modern126Visuals.h"
#include <d2d1_1.h>
#include <dwrite.h>
#include <cmath>
#include <cwchar>

// Additional read-only visual/HUD modules for the validated 1.26 bridge.
// These reuse the stable entity projection and LocalPlayer component paths.
namespace Modern126Extras {
	inline bool tracersEnabled = false;
	inline bool coordinatesEnabled = false;
	inline bool speedEnabled = false;
	inline bool directionEnabled = false;
	inline bool menuLeftWasDown = false;
	inline uint64_t toggleCount = 0;
	inline bool loggedTracersReady = false;
	inline bool loggedTelemetryReady = false;

	struct Telemetry {
		Modern126Gameplay::Vec3Lite pos;
		Modern126Gameplay::Vec3Lite velocity;
		float yaw;
	};

	inline bool anyEnabled() {
		return tracersEnabled || coordinatesEnabled || speedEnabled || directionEnabled;
	}

	inline void logToggle(const char* name, bool enabled) {
		++toggleCount;
		logF("[modern] %s toggle #%llu state=%s", name,
			static_cast<unsigned long long>(toggleCount), enabled ? "ON" : "OFF");
	}

	inline bool pointInside(const POINT& point, const D2D1_RECT_F& rect) {
		return static_cast<float>(point.x) >= rect.left && static_cast<float>(point.x) <= rect.right &&
			static_cast<float>(point.y) >= rect.top && static_cast<float>(point.y) <= rect.bottom;
	}

	inline bool readTelemetry(Telemetry* out) {
		if (out == nullptr)
			return false;
		void* localPlayer = Modern126Gameplay::refreshLocalPlayer();
		if (localPlayer == nullptr)
			return false;

		Modern126Gameplay::StateVectorLite* state = nullptr;
		Modern126Gameplay::ActorRotationLite* rotation = nullptr;
		if (!Modern126Gameplay::resolveFlyComponents(localPlayer, &state, &rotation) || state == nullptr || rotation == nullptr)
			return false;

		__try {
			Telemetry next = {};
			next.pos = state->pos;
			next.velocity = state->velocity;
			next.yaw = rotation->rotation.y;
			if (!std::isfinite(next.pos.x) || !std::isfinite(next.pos.y) || !std::isfinite(next.pos.z) ||
				!std::isfinite(next.velocity.x) || !std::isfinite(next.velocity.y) || !std::isfinite(next.velocity.z) ||
				!std::isfinite(next.yaw))
				return false;
			*out = next;
			if (!loggedTelemetryReady) {
				loggedTelemetryReady = true;
				logF("[modern] HUD telemetry bridge READY StateVector=%llX Rotation=%llX",
					reinterpret_cast<uintptr_t>(state), reinterpret_cast<uintptr_t>(rotation));
			}
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	inline const wchar_t* directionName(float yaw) {
		float normalized = std::fmod(yaw, 360.0f);
		if (normalized < 0.0f)
			normalized += 360.0f;
		const int sector = static_cast<int>(std::floor((normalized + 22.5f) / 45.0f)) & 7;
		static const wchar_t* names[8] = {
			L"South", L"South-West", L"West", L"North-West",
			L"North", L"North-East", L"East", L"South-East"
		};
		return names[sector];
	}

	inline void drawTracers(ID2D1DeviceContext* context, ID2D1Bitmap1* target, ID2D1SolidColorBrush* brush) {
		if (!tracersEnabled || context == nullptr || target == nullptr || brush == nullptr)
			return;

		Modern126Visuals::ProjectionContext projection = {};
		if (!Modern126Visuals::createProjectionContext(target, &projection))
			return;
		if (!Modern126Visuals::refreshEntityBoxes())
			return;

		const D2D1_POINT_2F start = D2D1::Point2F(projection.screen.width * 0.5f, projection.screen.height - 2.0f);
		for (const auto& box : Modern126Visuals::entityBoxes) {
			const Modern126Visuals::Vec3Lite center = {
				(box.lower.x + box.higher.x) * 0.5f,
				(box.lower.y + box.higher.y) * 0.5f,
				(box.lower.z + box.higher.z) * 0.5f
			};
			D2D1_POINT_2F end = {};
			if (!Modern126Visuals::projectPoint(center, projection, &end))
				continue;
			if (end.x < -projection.screen.width || end.x > projection.screen.width * 2.0f ||
				end.y < -projection.screen.height || end.y > projection.screen.height * 2.0f)
				continue;
			context->DrawLine(start, end, brush, 1.4f);
		}

		if (!loggedTracersReady) {
			loggedTracersReady = true;
			logF("[modern] Visuals/Tracers projection READY entities=%zu", Modern126Visuals::entityBoxes.size());
		}
	}

	inline void drawTelemetryHud(ID2D1DeviceContext* context, ID2D1Bitmap1* target,
		IDWriteTextFormat* textFormat, ID2D1SolidColorBrush* panelBrush, ID2D1SolidColorBrush* textBrush) {
		if ((!coordinatesEnabled && !speedEnabled && !directionEnabled) || context == nullptr || target == nullptr ||
			textFormat == nullptr || panelBrush == nullptr || textBrush == nullptr)
			return;

		Telemetry telemetry = {};
		if (!readTelemetry(&telemetry))
			return;

		const D2D1_SIZE_F size = target->GetSize();
		int rows = 0;
		if (coordinatesEnabled) ++rows;
		if (speedEnabled) ++rows;
		if (directionEnabled) ++rows;
		float top = size.height - 14.0f - static_cast<float>(rows) * 32.0f;
		if (top < 12.0f)
			top = 12.0f;

		auto drawRow = [&](const wchar_t* text, float width) {
			const D2D1_RECT_F bg = { 14.0f, top, width, top + 28.0f };
			const D2D1_RECT_F textRect = { 24.0f, top + 2.0f, width - 8.0f, top + 26.0f };
			context->FillRectangle(bg, panelBrush);
			context->DrawText(text, static_cast<UINT32>(wcslen(text)), textFormat, textRect, textBrush);
			top += 32.0f;
		};

		wchar_t text[128] = {};
		if (coordinatesEnabled) {
			swprintf_s(text, _countof(text), L"XYZ: %.1f  %.1f  %.1f", telemetry.pos.x, telemetry.pos.y, telemetry.pos.z);
			drawRow(text, 270.0f);
		}
		if (speedEnabled) {
			const float horizontalSpeed = std::sqrt(telemetry.velocity.x * telemetry.velocity.x + telemetry.velocity.z * telemetry.velocity.z) * 20.0f;
			swprintf_s(text, _countof(text), L"Speed: %.2f b/s", horizontalSpeed);
			drawRow(text, 190.0f);
		}
		if (directionEnabled) {
			float normalized = std::fmod(telemetry.yaw, 360.0f);
			if (normalized < 0.0f) normalized += 360.0f;
			swprintf_s(text, _countof(text), L"Facing: %ls  %.0f deg", directionName(telemetry.yaw), normalized);
			drawRow(text, 245.0f);
		}
	}

	inline void render(ID2D1DeviceContext* context, ID2D1Bitmap1* target,
		IDWriteTextFormat* textFormat, ID2D1SolidColorBrush* panelBrush,
		ID2D1SolidColorBrush* tracerBrush, ID2D1SolidColorBrush* textBrush) {
		drawTracers(context, target, tracerBrush);
		drawTelemetryHud(context, target, textFormat, panelBrush, textBrush);
	}

	inline void drawMenu(ID2D1DeviceContext* context, IDWriteTextFormat* titleFormat, IDWriteTextFormat* bodyFormat,
		ID2D1SolidColorBrush* panelBrush, ID2D1SolidColorBrush* headerBrush,
		ID2D1SolidColorBrush* hoverBrush, ID2D1SolidColorBrush* activeBrush, ID2D1SolidColorBrush* textBrush,
		const POINT& mouse, bool mouseValid, bool menuVisible) {
		if (!menuVisible || context == nullptr || titleFormat == nullptr || bodyFormat == nullptr)
		{
			menuLeftWasDown = false;
			return;
		}

		const D2D1_RECT_F panel = { 24.0f, 328.0f, 580.0f, 486.0f };
		const D2D1_RECT_F header = { 24.0f, 328.0f, 580.0f, 362.0f };
		const D2D1_RECT_F tracerButton = { 38.0f, 400.0f, 286.0f, 441.0f };
		const D2D1_RECT_F coordsButton = { 318.0f, 400.0f, 566.0f, 441.0f };
		const D2D1_RECT_F speedButton = { 318.0f, 448.0f, 438.0f, 481.0f };
		const D2D1_RECT_F directionButton = { 446.0f, 448.0f, 566.0f, 481.0f };

		const bool hoverTracer = mouseValid && pointInside(mouse, tracerButton);
		const bool hoverCoords = mouseValid && pointInside(mouse, coordsButton);
		const bool hoverSpeed = mouseValid && pointInside(mouse, speedButton);
		const bool hoverDirection = mouseValid && pointInside(mouse, directionButton);

		const bool leftDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
		if (leftDown && !menuLeftWasDown) {
			if (hoverTracer) {
				tracersEnabled = !tracersEnabled;
				logToggle("Visuals/Tracers", tracersEnabled);
			} else if (hoverCoords) {
				coordinatesEnabled = !coordinatesEnabled;
				logToggle("HUD/Coordinates", coordinatesEnabled);
			} else if (hoverSpeed) {
				speedEnabled = !speedEnabled;
				logToggle("HUD/Speed", speedEnabled);
			} else if (hoverDirection) {
				directionEnabled = !directionEnabled;
				logToggle("HUD/Direction", directionEnabled);
			}
		}
		menuLeftWasDown = leftDown;

		context->FillRectangle(panel, panelBrush);
		context->FillRectangle(header, headerBrush);
		static const wchar_t title[] = L"MORE VISUALS + HUD";
		static const wchar_t visuals[] = L"VISUALS";
		static const wchar_t hud[] = L"HUD";
		context->DrawText(title, _countof(title) - 1, titleFormat, D2D1::RectF(36.0f, 333.0f, 570.0f, 360.0f), textBrush);
		context->DrawText(visuals, _countof(visuals) - 1, bodyFormat, D2D1::RectF(38.0f, 370.0f, 286.0f, 396.0f), textBrush);
		context->DrawText(hud, _countof(hud) - 1, bodyFormat, D2D1::RectF(318.0f, 370.0f, 566.0f, 396.0f), textBrush);

		const auto drawToggle = [&](const D2D1_RECT_F& rect, bool enabled, bool hovered,
			const wchar_t* onText, const wchar_t* offText) {
			ID2D1SolidColorBrush* brush = enabled ? activeBrush : (hovered ? hoverBrush : headerBrush);
			context->FillRectangle(rect, brush);
			const wchar_t* text = enabled ? onText : offText;
			const D2D1_RECT_F textRect = { rect.left + 10.0f, rect.top + 6.0f, rect.right - 5.0f, rect.bottom - 3.0f };
			context->DrawText(text, static_cast<UINT32>(wcslen(text)), bodyFormat, textRect, textBrush);
		};

		drawToggle(tracerButton, tracersEnabled, hoverTracer, L"TRACERS: ON", L"TRACERS: OFF");
		drawToggle(coordsButton, coordinatesEnabled, hoverCoords, L"COORDINATES: ON", L"COORDINATES: OFF");
		drawToggle(speedButton, speedEnabled, hoverSpeed, L"SPEED: ON", L"SPEED: OFF");
		drawToggle(directionButton, directionEnabled, hoverDirection, L"DIR: ON", L"DIR: OFF");
	}

	inline void shutdown() {
		tracersEnabled = false;
		coordinatesEnabled = false;
		speedEnabled = false;
		directionEnabled = false;
		menuLeftWasDown = false;
	}
}
