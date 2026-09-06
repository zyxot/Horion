#pragma once

#include <string>

// Tiny 1.26.45.1 overlay used only to prove current UI rendering + KeyMap input
// before reconnecting Horion's archived ClickGUI. No gameplay modules are
// enabled here.
namespace Modern126Overlay {
	inline bool visible = false;
	inline bool textEnabled = true;
	inline bool loggedVisible = false;
	inline bool loggedText = false;
	inline bool loggedTextFailure = false;
	inline bool loggedGuiScale = false;

	struct RectangleArea {
		float left;
		float right;
		float top;
		float bottom;
	};

	struct Color {
		float r;
		float g;
		float b;
		float a;
	};

	enum class TextAlignment : int {
		LEFT = 0,
		RIGHT = 1,
		CENTER = 2
	};

	struct TextMeasureData {
		float textSize = 10.f;
		float linePadding = 0.f;
		bool displayShadow = false;
		bool showColorSymbols = false;
		bool hideHyphen = false;
	};

	struct CaretMeasureData {
		int position = -1;
		bool shouldRender = false;
	};

	inline bool addressInMinecraft(uintptr_t address) {
		if (address == 0)
			return false;
		MEMORY_BASIC_INFORMATION info = {};
		const SIZE_T queried = VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info));
		return queried == sizeof(info) && info.Type == MEM_IMAGE &&
			info.AllocationBase == GetModuleHandleA("Minecraft.Windows.exe");
	}

	// Current GuiData layout on the verified 1.26.45.1 build:
	//   +0x5C guiScale
	//   +0x60 guiScaleFrac (1 / guiScale)
	// MinecraftUIRenderContext consumes GUI-space coordinates, so use the
	// reciprocal scale to keep this test panel roughly the same physical size
	// regardless of the user's Minecraft GUI scale.
	inline float getGuiScaleFracGuarded(void* guiData) {
		if (guiData == nullptr)
			return 1.f;

		__try {
			const float frac = *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(guiData) + 0x60);
			if (frac >= 0.05f && frac <= 1.0f)
				return frac;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
		}
		return 1.f;
	}

	inline bool fillRectangleGuarded(void* renderContext, const RectangleArea& rect, const Color& color, float alpha = 1.f) {
		if (renderContext == nullptr)
			return false;

		__try {
			auto* vtable = *reinterpret_cast<uintptr_t**>(renderContext);
			if (vtable == nullptr || !addressInMinecraft(vtable[0xF]))
				return false;

			using FillRectangleFn = void(__fastcall*)(void*, const RectangleArea&, const Color&, float);
			auto fn = reinterpret_cast<FillRectangleFn>(vtable[0xF]);
			fn(renderContext, rect, color, alpha);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	// Keep the SEH frame in a helper that owns no std::string object. That avoids
	// MSVC C2712 while still protecting the 1.26 drawDebugText experiment.
	inline bool drawDebugTextGuarded(void* renderContext, const RectangleArea& rect,
		const std::string& text, const Color& color, float alpha) {
		if (renderContext == nullptr)
			return false;

		__try {
			auto* vtable = *reinterpret_cast<uintptr_t**>(renderContext);
			if (vtable == nullptr || !addressInMinecraft(vtable[0x4]))
				return false;

			// The previous 1.0 value was far below the scale used by current Bedrock
			// UI text. The maintained 1.26 layout defaults TextMeasureData to 10.0;
			// use 8.0 here so the canary text is visibly sized without dominating the
			// small panel.
			const TextMeasureData measure { 8.0f, 0.f, true, false, false };
			const CaretMeasureData caret { -1, false };
			using DrawDebugTextFn = void(__fastcall*)(void*, const RectangleArea&, const std::string&,
				const Color&, float, TextAlignment, const TextMeasureData&, const CaretMeasureData&);
			auto fn = reinterpret_cast<DrawDebugTextFn>(vtable[0x4]);
			fn(renderContext, rect, text, color, alpha, TextAlignment::LEFT, measure, caret);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	inline void toggle() {
		visible = !visible;
		logF("[modern] INSERT toggled test menu %s", visible ? "ON" : "OFF");
	}

	inline void render(void* renderContext, void* guiData) {
		if (!visible || renderContext == nullptr)
			return;

		const float scale = getGuiScaleFracGuarded(guiData);
		if (!loggedGuiScale) {
			loggedGuiScale = true;
			logF("[modern] Overlay GuiData scale fraction=%.3f", scale);
		}

		const auto scaledRect = [scale](float left, float right, float top, float bottom) {
			return RectangleArea { left * scale, right * scale, top * scale, bottom * scale };
		};

		const Color panel { 0.055f, 0.065f, 0.085f, 0.94f };
		const Color header { 0.10f, 0.65f, 1.00f, 0.95f };
		const Color row { 0.10f, 0.115f, 0.145f, 0.92f };
		const Color text { 0.95f, 0.97f, 1.00f, 1.00f };
		const Color muted { 0.70f, 0.76f, 0.84f, 1.00f };

		const bool panelOk =
			fillRectangleGuarded(renderContext, scaledRect(24.f, 324.f, 24.f, 214.f), panel) &&
			fillRectangleGuarded(renderContext, scaledRect(24.f, 324.f, 24.f, 58.f), header) &&
			fillRectangleGuarded(renderContext, scaledRect(38.f, 310.f, 78.f, 112.f), row) &&
			fillRectangleGuarded(renderContext, scaledRect(38.f, 310.f, 122.f, 156.f), row) &&
			fillRectangleGuarded(renderContext, scaledRect(38.f, 310.f, 166.f, 200.f), row);

		if (!panelOk)
			return;

		if (!loggedVisible) {
			loggedVisible = true;
			logF("[modern] INSERT test menu rendered successfully");
		}

		if (!textEnabled)
			return;

		static const std::string title = "Horion 1.26";
		static const std::string line1 = "Modern runtime bridge";
		static const std::string line2 = "Render + input verified";
		static const std::string line3 = "INSERT closes this menu";

		const bool textOk =
			drawDebugTextGuarded(renderContext, scaledRect(38.f, 310.f, 31.f, 53.f), title, text, 1.f) &&
			drawDebugTextGuarded(renderContext, scaledRect(48.f, 300.f, 84.f, 106.f), line1, text, 1.f) &&
			drawDebugTextGuarded(renderContext, scaledRect(48.f, 300.f, 128.f, 150.f), line2, text, 1.f) &&
			drawDebugTextGuarded(renderContext, scaledRect(48.f, 300.f, 172.f, 194.f), line3, muted, 1.f);

		if (textOk) {
			if (!loggedText) {
				loggedText = true;
				logF("[modern] 1.26 drawDebugText calls completed; visual confirmation still required");
			}
		} else {
			textEnabled = false;
			if (!loggedTextFailure) {
				loggedTextFailure = true;
				logF("[modern] drawDebugText test failed and was disabled; rectangle menu remains active");
			}
		}
	}
}
