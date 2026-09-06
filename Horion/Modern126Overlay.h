#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

// Small 1.26.45.1 overlay used to validate current Bedrock rendering/input
// before reconnecting Horion's archived ClickGUI. Gameplay modules remain off.
namespace Modern126Overlay {
	inline bool visible = false;
	inline bool textEnabled = true;
	inline bool loggedVisible = false;
	inline bool loggedText = false;
	inline bool loggedTextFailure = false;
	inline bool loggedGuiScale = false;
	inline bool loggedFontInfo = false;
	inline bool loggedTextAlpha = false;

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
		return info.State == MEM_COMMIT && (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) == 0;
	}

	inline float getGuiScaleFracGuarded(void* guiData) {
		if (guiData == nullptr)
			return 1.f;
		__try {
			const float frac = *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(guiData) + 0x60);
			return (frac >= 0.05f && frac <= 1.0f) ? frac : 1.f;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return 1.f;
		}
	}

	inline bool fillRectangleGuarded(void* renderContext, const RectangleArea& rect, const Color& color, float alpha = 1.f) {
		if (renderContext == nullptr)
			return false;
		__try {
			auto* vtable = *reinterpret_cast<uintptr_t**>(renderContext);
			if (vtable == nullptr || !addressInMinecraft(vtable[0xF]))
				return false;
			using Fn = void(__fastcall*)(void*, const RectangleArea&, const Color&, float);
			reinterpret_cast<Fn>(vtable[0xF])(renderContext, rect, color, alpha);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	// The maintained current client exposes FontRepository::fontList at +0x40,
	// fontToIdMap at +0x58 and resolves Minecraft's normal font through the
	// "DefaultFont" map entry. Keep the STL lookup in a helper without SEH so the
	// guarded caller can catch a bad cross-version layout cleanly.
	inline uint64_t resolveDefaultFontIdUnsafe(uintptr_t repo) {
		static const std::string key = "DefaultFont";
		auto* map = reinterpret_cast<std::unordered_map<std::string, uint64_t>*>(repo + 0x58);
		auto it = map->find(key);
		return it == map->end() ? UINT64_MAX : it->second;
	}

	inline uint64_t resolveDefaultFontIdGuarded(uintptr_t repo) {
		__try {
			return resolveDefaultFontIdUnsafe(repo);
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return UINT64_MAX;
		}
	}

	inline void* resolveFontGuarded(void* minecraftGame, uint64_t* outId, size_t* outCount, bool* outUsedDefault) {
		if (outId)
			*outId = UINT64_MAX;
		if (outCount)
			*outCount = 0;
		if (outUsedDefault)
			*outUsedDefault = false;
		if (minecraftGame == nullptr)
			return nullptr;

		__try {
			const uintptr_t repo = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(minecraftGame) + 0x700);
			if (!readableAddress(repo + 0x40))
				return nullptr;

			const uintptr_t begin = *reinterpret_cast<uintptr_t*>(repo + 0x40);
			const uintptr_t end = *reinterpret_cast<uintptr_t*>(repo + 0x48);
			if (begin == 0 || end < begin || !readableAddress(begin))
				return nullptr;

			constexpr size_t sharedPtrSize = sizeof(uintptr_t) * 2;
			const size_t count = static_cast<size_t>((end - begin) / sharedPtrSize);
			if (outCount)
				*outCount = count;
			if (count == 0)
				return nullptr;

			uint64_t id = resolveDefaultFontIdGuarded(repo);
			bool usedDefault = id != UINT64_MAX && id < count;
			if (!usedDefault) {
				// Fail-soft fallback to the smooth-font slot previously validated.
				id = 7;
				if (id >= count)
					return nullptr;
			}

			void* font = *reinterpret_cast<void**>(begin + (static_cast<size_t>(id) * sharedPtrSize));
			if (!readableAddress(reinterpret_cast<uintptr_t>(font)))
				return nullptr;
			if (outId)
				*outId = id;
			if (outUsedDefault)
				*outUsedDefault = usedDefault;
			return font;
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
			using Fn = float(__fastcall*)(void*);
			const float height = reinterpret_cast<Fn>(vtable[0x7])(font);
			return (height >= 1.f && height <= 100.f) ? height : 10.f;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return 10.f;
		}
	}

	inline float getTextAlphaGuarded(void* renderContext, bool* ok) {
		if (ok)
			*ok = false;
		if (renderContext == nullptr)
			return 1.f;
		__try {
			auto* vtable = *reinterpret_cast<uintptr_t**>(renderContext);
			if (vtable == nullptr || !addressInMinecraft(vtable[0x2]))
				return 1.f;
			using Fn = float(__fastcall*)(void*);
			const float alpha = reinterpret_cast<Fn>(vtable[0x2])(renderContext);
			if (ok)
				*ok = true;
			return alpha;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return 1.f;
		}
	}

	inline bool setTextAlphaGuarded(void* renderContext, float alpha) {
		if (renderContext == nullptr)
			return false;
		__try {
			auto* vtable = *reinterpret_cast<uintptr_t**>(renderContext);
			if (vtable == nullptr || !addressInMinecraft(vtable[0x3]))
				return false;
			using Fn = void(__fastcall*)(void*, float);
			reinterpret_cast<Fn>(vtable[0x3])(renderContext, alpha);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	inline bool drawTextGuarded(void* renderContext, void* font, const RectangleArea& rect,
		const std::string& value, const Color& color, float requestedSize,
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

			const TextMeasureData measure { measureSize, 0.f, false, false, false };
			const CaretMeasureData caret { -1, false };
			using Fn = void(__fastcall*)(void*, void*, const RectangleArea&, const std::string&,
				const Color&, float, TextAlignment, const TextMeasureData&, const CaretMeasureData&);
			reinterpret_cast<Fn>(vtable[0x5])(
				renderContext, font, rect, value, color, 1.f, TextAlignment::LEFT, measure, caret);
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
			using Fn = void(__fastcall*)(void*, float, std::optional<float>);
			reinterpret_cast<Fn>(vtable[0x6])(renderContext, 0.f, std::optional<float> {});
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

		// Diagnostic pass: intentionally draw no rectangles. If text now remains
		// visible, the rectangle batch/flush path was covering the text afterward.
		const Color text { 0.95f, 0.97f, 1.00f, 1.00f };
		const Color muted { 0.70f, 0.76f, 0.84f, 1.00f };

		if (!loggedVisible) {
			loggedVisible = true;
			logF("[modern] Text-only overlay test entered; rectangle drawing is disabled");
		}
		if (!textEnabled)
			return;

		uint64_t fontId = UINT64_MAX;
		size_t fontCount = 0;
		bool usedDefaultFont = false;
		void* font = resolveFontGuarded(minecraftGame, &fontId, &fontCount, &usedDefaultFont);
		if (font == nullptr) {
			textEnabled = false;
			if (!loggedTextFailure) {
				loggedTextFailure = true;
				logF("[modern] Normal drawText disabled: FontRepository font could not be resolved");
			}
			return;
		}

		const float lineHeight = getFontLineHeightGuarded(font);
		bool textAlphaOk = false;
		const float previousTextAlpha = getTextAlphaGuarded(renderContext, &textAlphaOk);
		if (textAlphaOk)
			setTextAlphaGuarded(renderContext, 1.f);

		if (!loggedFontInfo) {
			loggedFontInfo = true;
			const float bodyMeasure = (30.f * scale) / lineHeight;
			logF("[modern] FontRepository count=%zu selectedId=%llu source=%s",
				fontCount, static_cast<unsigned long long>(fontId), usedDefaultFont ? "DefaultFont" : "smooth fallback");
			logF("[modern] Normal text font=%llX lineHeight=%.3f bodyMeasure=%.3f",
				reinterpret_cast<uintptr_t>(font), lineHeight, bodyMeasure);
		}
		if (!loggedTextAlpha) {
			loggedTextAlpha = true;
			logF("[modern] RenderContext textAlpha before overlay=%.3f; overlay forces 1.000 for text pass",
				previousTextAlpha);
		}

		static const std::string title = "HORION TEXT-ONLY TEST";
		static const std::string line1 = "Modern runtime bridge";
		static const std::string line2 = "No rectangle draw calls";
		static const std::string line3 = "INSERT closes this test";

		const bool textCallsOk =
			drawTextGuarded(renderContext, font, scaledRect(30.f, 318.f, 24.f, 58.f), title, text, 32.f, scale, lineHeight) &&
			drawTextGuarded(renderContext, font, scaledRect(44.f, 304.f, 78.f, 112.f), line1, text, 30.f, scale, lineHeight) &&
			drawTextGuarded(renderContext, font, scaledRect(44.f, 304.f, 122.f, 156.f), line2, text, 30.f, scale, lineHeight) &&
			drawTextGuarded(renderContext, font, scaledRect(44.f, 304.f, 166.f, 200.f), line3, muted, 30.f, scale, lineHeight);
		const bool textOk = textCallsOk && flushTextGuarded(renderContext);

		if (textAlphaOk)
			setTextAlphaGuarded(renderContext, previousTextAlpha);

		if (textOk) {
			if (!loggedText) {
				loggedText = true;
				logF("[modern] Text-only drawText + flushText completed; no rectangles were submitted");
			}
		} else {
			textEnabled = false;
			if (!loggedTextFailure) {
				loggedTextFailure = true;
				logF("[modern] Text-only drawText path failed and was disabled");
			}
		}
	}
}
