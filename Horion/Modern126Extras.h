#pragma once

#include "Modern126Gameplay.h"
#include "Modern126Visuals.h"
#include "Modern126FeatureBatch.h"
#include <d2d1_1.h>
#include <dwrite.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>

// Additional read-only visual/HUD modules for the validated 1.26 bridge.
// Expensive entity work is shared across modules and capped at a bounded number
// of rendered entities so enabling several visual modules does not multiply the
// same projection/draw work without limit.
namespace Modern126Extras {
	inline bool tracersEnabled = false;
	inline bool threeDBoxesEnabled = false;
	inline bool fovCircleEnabled = false;
	inline bool coordinatesEnabled = false;
	inline bool speedEnabled = false;
	inline bool directionEnabled = false;
	inline bool entityCountEnabled = false;

	inline bool menuLeftWasDown = false;
	inline uint64_t toggleCount = 0;
	inline bool loggedTracersReady = false;
	inline bool loggedThreeDReady = false;
	inline bool loggedTelemetryReady = false;
	inline bool loggedEntityCountReady = false;
	inline bool loggedPerfReady = false;

	inline constexpr size_t maxRenderedEntities = 64;

	struct Telemetry {
		Modern126Gameplay::Vec3Lite pos;
		Modern126Gameplay::Vec3Lite velocity;
		float yaw;
	};

	inline Telemetry cachedTelemetry = {};
	inline ULONGLONG lastTelemetryRefresh = 0;
	inline bool cachedTelemetryValid = false;
	inline double perfAccumMs = 0.0;
	inline double perfMaxMs = 0.0;
	inline uint64_t perfFrames = 0;
	inline ULONGLONG perfWindowStart = 0;

	inline bool anyEnabled() {
		return tracersEnabled || threeDBoxesEnabled || fovCircleEnabled || coordinatesEnabled ||
			speedEnabled || directionEnabled || entityCountEnabled || Modern126FeatureBatch::anyEnabled();
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
		const ULONGLONG now = GetTickCount64();
		if (cachedTelemetryValid && now - lastTelemetryRefresh < 50) {
			*out = cachedTelemetry;
			return true;
		}

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
			cachedTelemetry = next;
			lastTelemetryRefresh = now;
			cachedTelemetryValid = true;
			*out = next;
			if (!loggedTelemetryReady) {
				loggedTelemetryReady = true;
				logF("[modern] HUD telemetry bridge READY StateVector=%llX Rotation=%llX cache=50ms",
					reinterpret_cast<uintptr_t>(state), reinterpret_cast<uintptr_t>(rotation));
			}
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			cachedTelemetryValid = false;
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

	inline void drawSharedEntityVisuals(ID2D1DeviceContext* context, ID2D1SolidColorBrush* brush,
		const Modern126Visuals::ProjectionContext& projection) {
		if (context == nullptr || brush == nullptr || (!tracersEnabled && !threeDBoxesEnabled))
			return;

		const D2D1_POINT_2F tracerStart = D2D1::Point2F(projection.screen.width * 0.5f, projection.screen.height - 2.0f);
		const size_t count = std::min(Modern126Visuals::entityBoxes.size(), maxRenderedEntities);
		for (size_t i = 0; i < count; ++i) {
			const auto& box = Modern126Visuals::entityBoxes[i];

			if (threeDBoxesEnabled) {
				std::array<D2D1_POINT_2F, 8> points = {};
				if (Modern126Visuals::projectBox(box, projection, &points))
					Modern126Visuals::drawWireBox(context, brush, points, 1.15f);
			}

			if (tracersEnabled) {
				const Modern126Visuals::Vec3Lite center = {
					(box.lower.x + box.higher.x) * 0.5f,
					(box.lower.y + box.higher.y) * 0.5f,
					(box.lower.z + box.higher.z) * 0.5f
				};
				D2D1_POINT_2F end = {};
				if (Modern126Visuals::projectPoint(center, projection, &end) &&
					end.x >= -projection.screen.width && end.x <= projection.screen.width * 2.0f &&
					end.y >= -projection.screen.height && end.y <= projection.screen.height * 2.0f)
					context->DrawLine(tracerStart, end, brush, 1.15f);
			}
		}

		if (tracersEnabled && !loggedTracersReady) {
			loggedTracersReady = true;
			logF("[modern] Visuals/Tracers READY sharedProjection=ON renderCap=%zu snapshot=%zu",
				maxRenderedEntities, Modern126Visuals::entityBoxes.size());
		}
		if (threeDBoxesEnabled && !loggedThreeDReady) {
			loggedThreeDReady = true;
			logF("[modern] Visuals/3DBoxes READY sharedProjection=ON renderCap=%zu snapshot=%zu",
				maxRenderedEntities, Modern126Visuals::entityBoxes.size());
		}
	}

	inline void drawFovCircle(ID2D1DeviceContext* context, ID2D1Bitmap1* target, ID2D1SolidColorBrush* brush) {
		if (!fovCircleEnabled || context == nullptr || target == nullptr || brush == nullptr)
			return;
		const D2D1_SIZE_F size = target->GetSize();
		const float radius = std::max(70.0f, std::min(size.width, size.height) * 0.16f);
		context->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(size.width * 0.5f, size.height * 0.5f), radius, radius), brush, 1.2f);
	}

	inline void drawHud(ID2D1DeviceContext* context, ID2D1Bitmap1* target,
		IDWriteTextFormat* textFormat, ID2D1SolidColorBrush* panelBrush, ID2D1SolidColorBrush* textBrush,
		bool entitySnapshotReady) {
		if ((!coordinatesEnabled && !speedEnabled && !directionEnabled && !entityCountEnabled) ||
			context == nullptr || target == nullptr || textFormat == nullptr || panelBrush == nullptr || textBrush == nullptr)
			return;

		Telemetry telemetry = {};
		const bool needTelemetry = coordinatesEnabled || speedEnabled || directionEnabled;
		if (needTelemetry && !readTelemetry(&telemetry))
			return;

		if (entityCountEnabled && entitySnapshotReady && !loggedEntityCountReady) {
			loggedEntityCountReady = true;
			logF("[modern] HUD/EntityCount READY sharedEntitySnapshot=ON");
		}

		const D2D1_SIZE_F size = target->GetSize();
		int rows = 0;
		if (coordinatesEnabled) ++rows;
		if (speedEnabled) ++rows;
		if (directionEnabled) ++rows;
		if (entityCountEnabled) ++rows;
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
			if (normalized < 0.0f)
				normalized += 360.0f;
			swprintf_s(text, _countof(text), L"Facing: %ls  %.0f deg", directionName(telemetry.yaw), normalized);
			drawRow(text, 245.0f);
		}
		if (entityCountEnabled) {
			swprintf_s(text, _countof(text), L"Entities: %zu", entitySnapshotReady ? Modern126Visuals::entityBoxes.size() : 0);
			drawRow(text, 175.0f);
		}
	}

	inline void recordPerf(const LARGE_INTEGER& begin) {
		LARGE_INTEGER end = {};
		LARGE_INTEGER frequency = {};
		QueryPerformanceCounter(&end);
		QueryPerformanceFrequency(&frequency);
		if (frequency.QuadPart <= 0)
			return;
		const double ms = static_cast<double>(end.QuadPart - begin.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart);
		perfAccumMs += ms;
		perfMaxMs = std::max(perfMaxMs, ms);
		++perfFrames;
		const ULONGLONG now = GetTickCount64();
		if (perfWindowStart == 0)
			perfWindowStart = now;
		if (now - perfWindowStart >= 2000 && perfFrames > 0) {
			const double average = perfAccumMs / static_cast<double>(perfFrames);
			logF("[modern] Extras perf avg=%.3fms max=%.3fms frames=%llu snapshot=%zu renderCap=%zu",
				average, perfMaxMs, static_cast<unsigned long long>(perfFrames),
				Modern126Visuals::entityBoxes.size(), maxRenderedEntities);
			perfAccumMs = 0.0;
			perfMaxMs = 0.0;
			perfFrames = 0;
			perfWindowStart = now;
			loggedPerfReady = true;
		}
	}

	inline void render(ID2D1DeviceContext* context, ID2D1Bitmap1* target,
		IDWriteTextFormat* textFormat, ID2D1SolidColorBrush* panelBrush,
		ID2D1SolidColorBrush* tracerBrush, ID2D1SolidColorBrush* textBrush) {
		if (context == nullptr || target == nullptr)
			return;
		LARGE_INTEGER begin = {};
		QueryPerformanceCounter(&begin);

		const bool needEntitySnapshot = tracersEnabled || threeDBoxesEnabled || entityCountEnabled || Modern126FeatureBatch::aimbotEnabled;
		const bool entitySnapshotReady = !needEntitySnapshot || Modern126Visuals::refreshEntityBoxes();

		// The feature batch reuses the exact snapshot acquired above, so Aimbot does
		// not enumerate actors independently from ESP/Tracers.
		Modern126FeatureBatch::tickFrame(entitySnapshotReady && needEntitySnapshot);

		if ((tracersEnabled || threeDBoxesEnabled) && entitySnapshotReady) {
			Modern126Visuals::ProjectionContext projection = {};
			if (Modern126Visuals::createProjectionContext(target, &projection))
				drawSharedEntityVisuals(context, tracerBrush, projection);
		}

		drawFovCircle(context, target, tracerBrush);
		drawHud(context, target, textFormat, panelBrush, textBrush, entitySnapshotReady && needEntitySnapshot);
		Modern126FeatureBatch::renderHelp(context, target, textFormat, textFormat, panelBrush, tracerBrush, textBrush);
		recordPerf(begin);
	}

	inline void drawMenu(ID2D1DeviceContext* context, IDWriteTextFormat* titleFormat, IDWriteTextFormat* bodyFormat,
		ID2D1SolidColorBrush* panelBrush, ID2D1SolidColorBrush* headerBrush,
		ID2D1SolidColorBrush* hoverBrush, ID2D1SolidColorBrush* activeBrush, ID2D1SolidColorBrush* textBrush,
		const POINT& mouse, bool mouseValid, bool menuVisible) {
		if (!menuVisible || context == nullptr || titleFormat == nullptr || bodyFormat == nullptr) {
			menuLeftWasDown = false;
			Modern126FeatureBatch::drawMenu(context, titleFormat, bodyFormat, panelBrush, headerBrush,
				hoverBrush, activeBrush, textBrush, mouse, mouseValid, false);
			return;
		}

		const D2D1_RECT_F panel = { 24.0f, 328.0f, 860.0f, 544.0f };
		const D2D1_RECT_F header = { 24.0f, 328.0f, 860.0f, 362.0f };
		context->FillRectangle(panel, panelBrush);
		context->FillRectangle(header, headerBrush);
		static const wchar_t title[] = L"MORE VISUALS + HUD";
		static const wchar_t visuals[] = L"VISUALS (64 DRAW CAP)";
		static const wchar_t hud[] = L"HUD";
		context->DrawText(title, _countof(title) - 1, titleFormat, D2D1::RectF(36.0f, 333.0f, 846.0f, 360.0f), textBrush);
		context->DrawText(visuals, _countof(visuals) - 1, bodyFormat, D2D1::RectF(38.0f, 370.0f, 286.0f, 396.0f), textBrush);
		context->DrawText(hud, _countof(hud) - 1, bodyFormat, D2D1::RectF(318.0f, 370.0f, 566.0f, 396.0f), textBrush);

		const D2D1_RECT_F tracerButton = { 38.0f, 400.0f, 286.0f, 437.0f };
		const D2D1_RECT_F boxesButton = { 38.0f, 444.0f, 286.0f, 481.0f };
		const D2D1_RECT_F fovButton = { 38.0f, 488.0f, 286.0f, 525.0f };
		const D2D1_RECT_F coordsButton = { 318.0f, 400.0f, 566.0f, 437.0f };
		const D2D1_RECT_F speedButton = { 318.0f, 444.0f, 438.0f, 481.0f };
		const D2D1_RECT_F directionButton = { 446.0f, 444.0f, 566.0f, 481.0f };
		const D2D1_RECT_F entityButton = { 318.0f, 488.0f, 566.0f, 525.0f };

		const bool hoverTracer = mouseValid && pointInside(mouse, tracerButton);
		const bool hoverBoxes = mouseValid && pointInside(mouse, boxesButton);
		const bool hoverFov = mouseValid && pointInside(mouse, fovButton);
		const bool hoverCoords = mouseValid && pointInside(mouse, coordsButton);
		const bool hoverSpeed = mouseValid && pointInside(mouse, speedButton);
		const bool hoverDirection = mouseValid && pointInside(mouse, directionButton);
		const bool hoverEntity = mouseValid && pointInside(mouse, entityButton);
		const bool leftDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
		if (leftDown && !menuLeftWasDown) {
			if (hoverTracer) { tracersEnabled = !tracersEnabled; logToggle("Visuals/Tracers", tracersEnabled); }
			else if (hoverBoxes) { threeDBoxesEnabled = !threeDBoxesEnabled; logToggle("Visuals/3DBoxes", threeDBoxesEnabled); }
			else if (hoverFov) { fovCircleEnabled = !fovCircleEnabled; logToggle("Visuals/FOVCircle", fovCircleEnabled); }
			else if (hoverCoords) { coordinatesEnabled = !coordinatesEnabled; logToggle("HUD/Coordinates", coordinatesEnabled); }
			else if (hoverSpeed) { speedEnabled = !speedEnabled; logToggle("HUD/Speed", speedEnabled); }
			else if (hoverDirection) { directionEnabled = !directionEnabled; logToggle("HUD/Direction", directionEnabled); }
			else if (hoverEntity) { entityCountEnabled = !entityCountEnabled; logToggle("HUD/EntityCount", entityCountEnabled); }
		}
		menuLeftWasDown = leftDown;

		auto drawToggle = [&](const D2D1_RECT_F& rect, bool enabled, bool hovered, const wchar_t* onText, const wchar_t* offText) {
			ID2D1SolidColorBrush* brush = enabled ? activeBrush : (hovered ? hoverBrush : headerBrush);
			context->FillRectangle(rect, brush);
			const wchar_t* text = enabled ? onText : offText;
			context->DrawText(text, static_cast<UINT32>(wcslen(text)), bodyFormat,
				D2D1::RectF(rect.left + 10.0f, rect.top + 6.0f, rect.right - 5.0f, rect.bottom - 3.0f), textBrush);
		};

		drawToggle(tracerButton, tracersEnabled, hoverTracer, L"TRACERS: ON", L"TRACERS: OFF");
		drawToggle(boxesButton, threeDBoxesEnabled, hoverBoxes, L"3D BOXES: ON", L"3D BOXES: OFF");
		drawToggle(fovButton, fovCircleEnabled, hoverFov, L"FOV CIRCLE: ON", L"FOV CIRCLE: OFF");
		drawToggle(coordsButton, coordinatesEnabled, hoverCoords, L"COORDINATES: ON", L"COORDINATES: OFF");
		drawToggle(speedButton, speedEnabled, hoverSpeed, L"SPEED: ON", L"SPEED: OFF");
		drawToggle(directionButton, directionEnabled, hoverDirection, L"DIR: ON", L"DIR: OFF");
		drawToggle(entityButton, entityCountEnabled, hoverEntity, L"ENTITY COUNT: ON", L"ENTITY COUNT: OFF");

		Modern126FeatureBatch::drawMenu(context, titleFormat, bodyFormat, panelBrush, headerBrush,
			hoverBrush, activeBrush, textBrush, mouse, mouseValid, true);
	}

	inline void shutdown() {
		Modern126FeatureBatch::shutdown();
		tracersEnabled = false;
		threeDBoxesEnabled = false;
		fovCircleEnabled = false;
		coordinatesEnabled = false;
		speedEnabled = false;
		directionEnabled = false;
		entityCountEnabled = false;
		menuLeftWasDown = false;
		cachedTelemetryValid = false;
		lastTelemetryRefresh = 0;
		perfAccumMs = 0.0;
		perfMaxMs = 0.0;
		perfFrames = 0;
		perfWindowStart = 0;
	}
}
