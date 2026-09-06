#pragma once

#include <optional>
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
	inline bool loggedFontInfo = false;

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

	inline bool readableAddress(uintptr_t address) {
		if (address == 0)
			return false;
		MEMORY_BASIC_INFORMATION info = {};
		if (VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info)) != sizeof(info))
			return false;
		if (info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
			return false;
		return true;
	}

	// Current GuiData layout on the verified 1.26.45.1 build:
	//   +0x5C guiScale
	//   +0x60 guiScaleFrac (1 / guiScale)
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

	// MinecraftGame +0x700 holds the current FontRepository pointer. The
	// maintained 1.26 client exposes its smooth font as fontList[7]. Read the
	// release-mode vector storage directly so this small bridge does not need to
	// import the newer SDK's FontRepository class yet.
	inline void* resolveFontGuarded(void* minecraftGame) {
		if (minecraftGame == nullptr)
			return nullptr;

		__try {
			const uintptr_t repo = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(minecraftGame) + 0x700);
			if (!readableAddress(repo + 0x40))
				return nullptr;

			// MSVC release std::vector begins with begin/end/capacity pointers.
			const uintptr_t begin = *reinterpret_cast<uintptr_t*>(repo + 0x40);
			const uintptr_t end = *reinterpret_cast<uintptr_t*>(repo + 0x48);
			if (begin == 0 || end < begin || !readableAddress(begin))
				return nullptr;

			// std::shared_ptr is two pointers in this build: object + control block.
			constexpr size_t sharedPtrSize = sizeof(uintptr_t) * 2;
			const size_t count = static_cast<size_t>((end - begin) / sharedPtrSize);
			if (count <= 7)
				return nullptr;

			void* font = *reinterpret_cast<void**>(begin + (7 * sharedPtrSize));
			return readableAddress(reinterpret_cast<uintptr_t>(font)) ? font : nullptr;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return nullptr;
		}
	}

	inline float getFontLineHeightGuarded(void* font) {
		if (font == nullptr)
			return 10.f;

		__try {
			auto* vtable = *reinterpret_cast<uintptr_t**>(font);
			if (vtable == nullptr || !addressInMinecraft(vtable[0x7]))
				return 10.f;
			using GetLineHeightFn = float(__fastcall*)(void*);
			const float height = reinterpret_cast<GetLineHeightFn>(vtable[0x7])(font);
			return (height >= 1.f && height <= 100.f) ? height : 10.f;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return 10.f;
		}
	}

	// Use MinecraftUIRenderContext::drawText (slot 0x5) with an actual Font.
	// The maintained current renderer computes TextMeasureData as:
	//   (requestedSize * guiScaleFrac) / fontLineHeight
	// Its DrawUtil defaults to a requested size of 30, so use that same scale for
	// the body text rather than the earlier 10px canary. On the user's 4x GUI
	// scale (guiScaleFrac=0.25, lineHeight=7.5), 30 becomes a normal measure of 1.0.
	inline bool drawTextGuarded(void* renderContext, void* font, const RectangleArea& rect,
		const std::string& text, const Color& color, float alpha, float requestedSize,
		float guiScaleFrac, float fontLineHeight) {
		if (renderContext == nullptr || font == nullptr)
			return false;

		__try {
			auto* vtable = *reinterpret_cast<uintptr_t**>(renderContext);
			if (vtable == nullptr || !addressInMinecraft(vtable[0x5]))
				return false;

			float measureSize = (requestedSize * guiScaleFrac) / fontLineHeight;
			if (measureSize < 0.05f)
				measureSize = 0.05f;
			if (measureSize > 4.f)
				measureSize = 4.f;

			const TextMeasureData measure { measureSize, 0.f, true, false, false };
			const CaretMeasureData caret { -1, false };
			using DrawTextFn = void(__fastcall*)(void*, void*, const RectangleArea&, const std::string&,
				const Color&, float, TextAlignment, const TextMeasureData&, const CaretMeasureData&);
			auto fn = reinterpret_cast<DrawTextFn>(vtable[0x5]);
			fn(renderContext, font, rect, text, color, alpha, TextAlignment::LEFT, measure, caret);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	inline bool flushTextGuarded(void* renderContext) {
		if (renderContext == nullptr)
			return false;

		__try {
			auto* vtable = *reinterpret_cast<uintptr_t**>(renderContext);
			if (vtable == nullptr || !addressInMinecraft(vtable[0x6]))
				return false;

			using FlushTextFn = void(__fastcall*)(void*, float, std::optional<float>);
			auto fn = reinterpret_cast<FlushTextFn>(vtable[0x6]);
			fn(renderContext, 0.f, std::optional<float> {});
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

	inline void render(void* renderContext, void* guiData, void* minecraftGame) {
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

		void* font = resolveFontGuarded(minecraftGame);
		if (font == nullptr) {
			textEnabled = false;
			if (!loggedTextFailure) {
				loggedTextFailure = true;
				logF("[modern] Normal drawText disabled: current FontRepository font could not be resolved");
			}
			return;
		}

		const float lineHeight = getFontLineHeightGuarded(font);
		if (!loggedFontInfo) {
			loggedFontInfo = true;
			const float bodyMeasure = (30.f * scale) / lineHeight;
			const float renderedBodyHeight = lineHeight * bodyMeasure;
			const float rowHeight = (112.f - 78.f) * scale;
			logF("[modern] Normal text font=%llX lineHeight=%.3f bodyMeasure=%.3f renderedHeight=%.3f rowHeight=%.3f",
				reinterpret_cast<uintptr_t>(font), lineHeight, bodyMeasure, renderedBodyHeight, rowHeight);
		}

		static const std::string title = "Horion 1.26";
		static const std::string line1 = "Modern runtime bridge";
		static const std::string line2 = "Render + input verified";
		static const std::string line3 = "INSERT closes this menu";

		// Use the full header/row heights for text clipping. The previous 22px
		// unscaled text rectangles became only 5.5 UI units at guiScaleFrac=0.25,
		// while a normal 30px Bedrock font renders ~7.5 UI units tall. That meant
		// drawText succeeded but its glyphs were clipped away.
		const bool textCallsOk =
			drawTextGuarded(renderContext, font, scaledRect(30.f, 318.f, 24.f, 58.f), title, text, 1.f, 32.f, scale, lineHeight) &&
			drawTextGuarded(renderContext, font, scaledRect(44.f, 304.f, 78.f, 112.f), line1, text, 1.f, 30.f, scale, lineHeight) &&
			drawTextGuarded(renderContext, font, scaledRect(44.f, 304.f, 122.f, 156.f), line2, text, 1.f, 30.f, scale, lineHeight) &&
			drawTextGuarded(renderContext, font, scaledRect(44.f, 304.f, 166.f, 200.f), line3, muted, 1.f, 30.f, scale, lineHeight);
		const bool textOk = textCallsOk && flushTextGuarded(renderContext);

		if (textOk) {
			if (!loggedText) {
				loggedText = true;
				logF("[modern] 1.26 normal drawText with unclipped row bounds + flushText completed");
			}
		} else {
			textEnabled = false;
			if (!loggedTextFailure) {
				loggedTextFailure = true;
				logF("[modern] Normal drawText path failed and was disabled; rectangle menu remains active");
			}
		}
	}
}
