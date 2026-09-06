#pragma once

#include <d3d11.h>
#include <d3d11on12.h>
#include <d3d12.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <dxgi.h>
#include <dxgi1_4.h>
#include <cwchar>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dxgi.lib")

// Stable current-Bedrock overlay path: IDXGISwapChain::Present -> D3D11On12 ->
// Direct2D/DirectWrite. This layer contains only renderer/HUD features; gameplay
// memory modules stay disabled until their 1.26 layouts are validated separately.
namespace Modern126PresentProbe {
	inline std::unique_ptr<FuncHook> presentHook;
	inline std::unique_ptr<FuncHook> executeCommandListsHook;
	inline ID3D12CommandQueue* capturedCommandQueue = nullptr;
	inline bool started = false;
	inline bool loggedPresent = false;
	inline bool loggedQueue = false;
	inline bool loggedRendererReady = false;
	inline bool loggedFirstDraw = false;
	inline bool loggedRendererFailure = false;
	inline bool loggedMouseReady = false;
	inline uint64_t presentCount = 0;
	inline uint64_t featureToggleCount = 0;
	inline DWORD lastPresentLogTick = 0;

	inline IDXGISwapChain3* rendererChain3 = nullptr;
	inline ID3D12Device* rendererDevice12 = nullptr;
	inline ID3D12CommandQueue* rendererQueue = nullptr;
	inline ID3D11Device* bridgeDevice11 = nullptr;
	inline ID3D11DeviceContext* bridgeContext11 = nullptr;
	inline ID3D11On12Device* bridge11On12 = nullptr;
	inline IDXGIDevice* bridgeDxgiDevice = nullptr;
	inline ID2D1Factory1* d2dFactory = nullptr;
	inline ID2D1Device* d2dDevice = nullptr;
	inline ID2D1DeviceContext* d2dContext = nullptr;
	inline IDWriteFactory* dwriteFactory = nullptr;
	inline IDWriteTextFormat* titleFormat = nullptr;
	inline IDWriteTextFormat* bodyFormat = nullptr;
	inline ID2D1SolidColorBrush* panelBrush = nullptr;
	inline ID2D1SolidColorBrush* headerBrush = nullptr;
	inline ID2D1SolidColorBrush* textBrush = nullptr;
	inline ID2D1SolidColorBrush* hoverBrush = nullptr;
	inline ID2D1SolidColorBrush* activeBrush = nullptr;
	inline std::vector<ID3D11Resource*> wrappedTargets;
	inline std::vector<ID2D1Bitmap1*> d2dTargets;
	inline IDXGISwapChain* rendererChainIdentity = nullptr;
	inline HWND rendererWindow = nullptr;
	inline bool rendererReady = false;
	inline POINT mouseClient = {};
	inline bool mouseValid = false;
	inline bool leftWasDown = false;

	// Modern renderer-only modules.
	inline bool crosshairEnabled = false;
	inline bool watermarkEnabled = false;
	inline bool fpsEnabled = false;
	inline bool moduleListEnabled = false;
	inline ULONGLONG fpsWindowStart = 0;
	inline uint32_t fpsWindowFrames = 0;
	inline uint32_t currentFps = 0;

	template <typename T>
	inline void releaseCom(T*& value) {
		if (value != nullptr) {
			value->Release();
			value = nullptr;
		}
	}

	inline bool anyPersistentFeatureEnabled() {
		return crosshairEnabled || watermarkEnabled || fpsEnabled || moduleListEnabled;
	}

	inline void tickFps() {
		const ULONGLONG now = GetTickCount64();
		if (fpsWindowStart == 0)
			fpsWindowStart = now;
		++fpsWindowFrames;
		const ULONGLONG elapsed = now - fpsWindowStart;
		if (elapsed >= 1000) {
			currentFps = elapsed != 0
				? static_cast<uint32_t>((static_cast<uint64_t>(fpsWindowFrames) * 1000ull) / elapsed)
				: 0;
			fpsWindowFrames = 0;
			fpsWindowStart = now;
		}
	}

	inline void releaseRenderer() {
		if (d2dContext != nullptr)
			d2dContext->SetTarget(nullptr);

		for (auto*& target : d2dTargets)
			releaseCom(target);
		d2dTargets.clear();

		for (auto*& target : wrappedTargets)
			releaseCom(target);
		wrappedTargets.clear();

		releaseCom(panelBrush);
		releaseCom(headerBrush);
		releaseCom(textBrush);
		releaseCom(hoverBrush);
		releaseCom(activeBrush);
		releaseCom(titleFormat);
		releaseCom(bodyFormat);
		releaseCom(dwriteFactory);
		releaseCom(d2dContext);
		releaseCom(d2dDevice);
		releaseCom(d2dFactory);
		releaseCom(bridgeDxgiDevice);
		releaseCom(bridge11On12);
		releaseCom(bridgeContext11);
		releaseCom(bridgeDevice11);
		releaseCom(rendererQueue);
		releaseCom(rendererDevice12);
		releaseCom(rendererChain3);
		rendererChainIdentity = nullptr;
		rendererWindow = nullptr;
		rendererReady = false;
		mouseValid = false;
		leftWasDown = false;
		loggedRendererReady = false;
		loggedFirstDraw = false;
	}

	inline void releaseCapturedQueue() {
		if (capturedCommandQueue != nullptr) {
			capturedCommandQueue->Release();
			capturedCommandQueue = nullptr;
		}
	}

	inline bool failRenderer(const char* stage, HRESULT hr) {
		if (!loggedRendererFailure) {
			loggedRendererFailure = true;
			logF("[modern] Present renderer init failed at %s hr=0x%08X", stage, static_cast<unsigned>(hr));
		}
		releaseRenderer();
		return false;
	}

	inline bool initializeRenderer(IDXGISwapChain* chain) {
		if (chain == nullptr || capturedCommandQueue == nullptr)
			return false;

		if (rendererReady && rendererChainIdentity == chain)
			return true;

		if (rendererChainIdentity != nullptr && rendererChainIdentity != chain)
			releaseRenderer();

		HRESULT hr = chain->QueryInterface(IID_PPV_ARGS(&rendererChain3));
		if (FAILED(hr) || rendererChain3 == nullptr)
			return failRenderer("IDXGISwapChain3", hr);

		hr = chain->GetDevice(IID_PPV_ARGS(&rendererDevice12));
		if (FAILED(hr) || rendererDevice12 == nullptr)
			return failRenderer("ID3D12Device", hr);

		capturedCommandQueue->AddRef();
		rendererQueue = capturedCommandQueue;

		ID3D12Device* queueDevice = nullptr;
		hr = rendererQueue->GetDevice(IID_PPV_ARGS(&queueDevice));
		if (FAILED(hr) || queueDevice == nullptr)
			return failRenderer("queue GetDevice", hr);
		const bool sameDevice = queueDevice == rendererDevice12;
		queueDevice->Release();
		if (!sameDevice)
			return failRenderer("queue/device mismatch", E_FAIL);

		IUnknown* queues[] = { rendererQueue };
		hr = D3D11On12CreateDevice(rendererDevice12, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
			nullptr, 0, queues, _countof(queues), 0,
			&bridgeDevice11, &bridgeContext11, nullptr);
		if (FAILED(hr) || bridgeDevice11 == nullptr || bridgeContext11 == nullptr)
			return failRenderer("D3D11On12CreateDevice", hr);

		hr = bridgeDevice11->QueryInterface(IID_PPV_ARGS(&bridge11On12));
		if (FAILED(hr) || bridge11On12 == nullptr)
			return failRenderer("ID3D11On12Device", hr);

		hr = bridgeDevice11->QueryInterface(IID_PPV_ARGS(&bridgeDxgiDevice));
		if (FAILED(hr) || bridgeDxgiDevice == nullptr)
			return failRenderer("IDXGIDevice", hr);

		D2D1_FACTORY_OPTIONS factoryOptions = {};
		hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1),
			&factoryOptions, reinterpret_cast<void**>(&d2dFactory));
		if (FAILED(hr) || d2dFactory == nullptr)
			return failRenderer("D2D1CreateFactory", hr);

		hr = d2dFactory->CreateDevice(bridgeDxgiDevice, &d2dDevice);
		if (FAILED(hr) || d2dDevice == nullptr)
			return failRenderer("ID2D1Device", hr);

		hr = d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &d2dContext);
		if (FAILED(hr) || d2dContext == nullptr)
			return failRenderer("ID2D1DeviceContext", hr);

		hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
			reinterpret_cast<IUnknown**>(&dwriteFactory));
		if (FAILED(hr) || dwriteFactory == nullptr)
			return failRenderer("DWriteCreateFactory", hr);

		hr = dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr,
			DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
			22.0f, L"en-us", &titleFormat);
		if (FAILED(hr) || titleFormat == nullptr)
			return failRenderer("title text format", hr);

		hr = dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr,
			DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
			18.0f, L"en-us", &bodyFormat);
		if (FAILED(hr) || bodyFormat == nullptr)
			return failRenderer("body text format", hr);

		titleFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
		bodyFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

		const D2D1_COLOR_F panelColor = { 0.055f, 0.065f, 0.085f, 0.94f };
		const D2D1_COLOR_F headerColor = { 0.08f, 0.34f, 0.72f, 0.96f };
		const D2D1_COLOR_F textColor = { 0.96f, 0.98f, 1.0f, 1.0f };
		const D2D1_COLOR_F hoverColor = { 0.12f, 0.45f, 0.88f, 1.0f };
		const D2D1_COLOR_F activeColor = { 0.12f, 0.62f, 0.34f, 1.0f };
		hr = d2dContext->CreateSolidColorBrush(panelColor, &panelBrush);
		if (FAILED(hr)) return failRenderer("panel brush", hr);
		hr = d2dContext->CreateSolidColorBrush(headerColor, &headerBrush);
		if (FAILED(hr)) return failRenderer("header brush", hr);
		hr = d2dContext->CreateSolidColorBrush(textColor, &textBrush);
		if (FAILED(hr)) return failRenderer("text brush", hr);
		hr = d2dContext->CreateSolidColorBrush(hoverColor, &hoverBrush);
		if (FAILED(hr)) return failRenderer("hover brush", hr);
		hr = d2dContext->CreateSolidColorBrush(activeColor, &activeBrush);
		if (FAILED(hr)) return failRenderer("active brush", hr);

		DXGI_SWAP_CHAIN_DESC chainDesc = {};
		hr = chain->GetDesc(&chainDesc);
		if (FAILED(hr) || chainDesc.BufferCount == 0)
			return failRenderer("swap-chain description", hr);
		rendererWindow = chainDesc.OutputWindow;

		wrappedTargets.reserve(chainDesc.BufferCount);
		d2dTargets.reserve(chainDesc.BufferCount);
		for (UINT i = 0; i < chainDesc.BufferCount; ++i) {
			ID3D12Resource* backBuffer = nullptr;
			hr = chain->GetBuffer(i, IID_PPV_ARGS(&backBuffer));
			if (FAILED(hr) || backBuffer == nullptr)
				return failRenderer("swap-chain back buffer", hr);

			D3D11_RESOURCE_FLAGS resourceFlags = {};
			resourceFlags.BindFlags = D3D11_BIND_RENDER_TARGET;
			ID3D11Resource* wrapped = nullptr;
			hr = bridge11On12->CreateWrappedResource(backBuffer, &resourceFlags,
				D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT,
				IID_PPV_ARGS(&wrapped));
			const DXGI_FORMAT bufferFormat = backBuffer->GetDesc().Format;
			backBuffer->Release();
			if (FAILED(hr) || wrapped == nullptr)
				return failRenderer("CreateWrappedResource", hr);

			IDXGISurface* surface = nullptr;
			hr = wrapped->QueryInterface(IID_PPV_ARGS(&surface));
			if (FAILED(hr) || surface == nullptr) {
				wrapped->Release();
				return failRenderer("wrapped IDXGISurface", hr);
			}

			D2D1_BITMAP_PROPERTIES1 props = {};
			props.pixelFormat.format = bufferFormat;
			props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
			props.dpiX = 96.0f;
			props.dpiY = 96.0f;
			props.bitmapOptions = static_cast<D2D1_BITMAP_OPTIONS>(
				D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW);

			ID2D1Bitmap1* bitmap = nullptr;
			hr = d2dContext->CreateBitmapFromDxgiSurface(surface, &props, &bitmap);
			surface->Release();
			if (FAILED(hr) || bitmap == nullptr) {
				wrapped->Release();
				return failRenderer("CreateBitmapFromDxgiSurface", hr);
			}

			wrappedTargets.push_back(wrapped);
			d2dTargets.push_back(bitmap);
		}

		rendererChainIdentity = chain;
		rendererReady = true;
		loggedRendererFailure = false;
		if (!loggedRendererReady) {
			loggedRendererReady = true;
			logF("[modern] Present Direct2D renderer READY chain=%llX buffers=%zu queue=%llX hwnd=%llX",
				reinterpret_cast<uintptr_t>(chain), d2dTargets.size(),
				reinterpret_cast<uintptr_t>(rendererQueue), reinterpret_cast<uintptr_t>(rendererWindow));
		}
		return true;
	}

	inline bool pointInside(const POINT& point, const D2D1_RECT_F& rect) {
		return static_cast<float>(point.x) >= rect.left && static_cast<float>(point.x) <= rect.right &&
			static_cast<float>(point.y) >= rect.top && static_cast<float>(point.y) <= rect.bottom;
	}

	inline bool updateMouse() {
		if (rendererWindow == nullptr)
			return false;
		POINT cursor = {};
		if (!GetCursorPos(&cursor) || !ScreenToClient(rendererWindow, &cursor)) {
			mouseValid = false;
			return false;
		}
		mouseClient = cursor;
		mouseValid = true;
		if (!loggedMouseReady) {
			loggedMouseReady = true;
			logF("[modern] Present mouse observation active; feature clicks are not intercepted");
		}
		return true;
	}

	inline void drawCrosshair(ID2D1Bitmap1* target) {
		if (!crosshairEnabled || target == nullptr)
			return;
		const D2D1_SIZE_F size = target->GetSize();
		const float centerX = size.width * 0.5f;
		const float centerY = size.height * 0.5f;
		const D2D1_RECT_F horizontal = { centerX - 8.0f, centerY - 1.0f, centerX + 8.0f, centerY + 1.0f };
		const D2D1_RECT_F vertical = { centerX - 1.0f, centerY - 8.0f, centerX + 1.0f, centerY + 8.0f };
		d2dContext->FillRectangle(horizontal, textBrush);
		d2dContext->FillRectangle(vertical, textBrush);
	}

	inline void drawWatermark() {
		if (!watermarkEnabled)
			return;
		const D2D1_RECT_F background = { 14.0f, 14.0f, 154.0f, 48.0f };
		const D2D1_RECT_F accent = { 14.0f, 14.0f, 18.0f, 48.0f };
		const D2D1_RECT_F textRect = { 28.0f, 18.0f, 150.0f, 46.0f };
		d2dContext->FillRectangle(background, panelBrush);
		d2dContext->FillRectangle(accent, headerBrush);
		static const wchar_t text[] = L"Horion 1.26";
		d2dContext->DrawText(text, _countof(text) - 1, bodyFormat, textRect, textBrush);
	}

	inline void drawFps() {
		if (!fpsEnabled)
			return;
		wchar_t text[48] = {};
		swprintf_s(text, _countof(text), L"FPS: %u", currentFps);
		const float top = watermarkEnabled ? 54.0f : 14.0f;
		const D2D1_RECT_F background = { 14.0f, top, 120.0f, top + 30.0f };
		const D2D1_RECT_F textRect = { 24.0f, top + 3.0f, 116.0f, top + 28.0f };
		d2dContext->FillRectangle(background, panelBrush);
		d2dContext->DrawText(text, static_cast<UINT32>(wcslen(text)), bodyFormat, textRect, textBrush);
	}

	inline void drawModuleList(ID2D1Bitmap1* target) {
		if (!moduleListEnabled || target == nullptr)
			return;
		const D2D1_SIZE_F size = target->GetSize();
		const float left = size.width > 230.0f ? size.width - 215.0f : 10.0f;
		float top = 16.0f;
		const D2D1_RECT_F titleBg = { left, top, size.width - 14.0f, top + 30.0f };
		const D2D1_RECT_F titleRect = { left + 10.0f, top + 3.0f, size.width - 20.0f, top + 28.0f };
		d2dContext->FillRectangle(titleBg, headerBrush);
		static const wchar_t title[] = L"ENABLED MODULES";
		d2dContext->DrawText(title, _countof(title) - 1, bodyFormat, titleRect, textBrush);
		top += 34.0f;

		const auto drawEntry = [&](const wchar_t* text) {
			const D2D1_RECT_F bg = { left, top, size.width - 14.0f, top + 28.0f };
			const D2D1_RECT_F rect = { left + 10.0f, top + 2.0f, size.width - 20.0f, top + 26.0f };
			d2dContext->FillRectangle(bg, panelBrush);
			d2dContext->DrawText(text, static_cast<UINT32>(wcslen(text)), bodyFormat, rect, textBrush);
			top += 30.0f;
		};

		if (crosshairEnabled) drawEntry(L"Crosshair");
		if (watermarkEnabled) drawEntry(L"Watermark");
		if (fpsEnabled) drawEntry(L"FPS Counter");
		if (moduleListEnabled) drawEntry(L"Module List");
	}

	inline void logFeatureToggle(const char* name, bool enabled) {
		++featureToggleCount;
		logF("[modern] %s toggle #%llu state=%s", name,
			static_cast<unsigned long long>(featureToggleCount), enabled ? "ON" : "OFF");
	}

	inline void drawPresentOverlay(IDXGISwapChain* chain) {
		if (chain == nullptr || capturedCommandQueue == nullptr)
			return;
		if (!Modern126Overlay::visible && !anyPersistentFeatureEnabled())
			return;
		if (!initializeRenderer(chain))
			return;

		const UINT index = rendererChain3->GetCurrentBackBufferIndex();
		if (index >= wrappedTargets.size() || index >= d2dTargets.size()) {
			if (!loggedRendererFailure) {
				loggedRendererFailure = true;
				logF("[modern] Present renderer invalid back-buffer index=%u count=%zu", index, wrappedTargets.size());
			}
			return;
		}

		const D2D1_RECT_F crosshairButton = { 38.0f, 116.0f, 286.0f, 157.0f };
		const D2D1_RECT_F watermarkButton = { 318.0f, 116.0f, 566.0f, 157.0f };
		const D2D1_RECT_F fpsButton = { 318.0f, 168.0f, 566.0f, 209.0f };
		const D2D1_RECT_F moduleListButton = { 318.0f, 220.0f, 566.0f, 261.0f };
		bool hoverCrosshair = false;
		bool hoverWatermark = false;
		bool hoverFps = false;
		bool hoverModuleList = false;

		if (Modern126Overlay::visible) {
			updateMouse();
			hoverCrosshair = mouseValid && pointInside(mouseClient, crosshairButton);
			hoverWatermark = mouseValid && pointInside(mouseClient, watermarkButton);
			hoverFps = mouseValid && pointInside(mouseClient, fpsButton);
			hoverModuleList = mouseValid && pointInside(mouseClient, moduleListButton);
			const bool leftDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
			if (leftDown && !leftWasDown) {
				if (hoverCrosshair) {
					crosshairEnabled = !crosshairEnabled;
					logFeatureToggle("Visuals/Crosshair", crosshairEnabled);
				} else if (hoverWatermark) {
					watermarkEnabled = !watermarkEnabled;
					logFeatureToggle("HUD/Watermark", watermarkEnabled);
				} else if (hoverFps) {
					fpsEnabled = !fpsEnabled;
					logFeatureToggle("HUD/FPS Counter", fpsEnabled);
				} else if (hoverModuleList) {
					moduleListEnabled = !moduleListEnabled;
					logFeatureToggle("HUD/Module List", moduleListEnabled);
				}
			}
			leftWasDown = leftDown;
		} else {
			mouseValid = false;
			leftWasDown = false;
		}

		ID3D11Resource* wrapped = wrappedTargets[index];
		bridge11On12->AcquireWrappedResources(&wrapped, 1);
		d2dContext->SetTarget(d2dTargets[index]);
		d2dContext->BeginDraw();

		if (Modern126Overlay::visible) {
			const D2D1_RECT_F panel = { 24.0f, 24.0f, 580.0f, 318.0f };
			const D2D1_RECT_F header = { 24.0f, 24.0f, 580.0f, 62.0f };
			d2dContext->FillRectangle(panel, panelBrush);
			d2dContext->FillRectangle(header, headerBrush);

			static const wchar_t title[] = L"HORION 1.26";
			static const wchar_t visuals[] = L"VISUALS";
			static const wchar_t hud[] = L"HUD";
			const D2D1_RECT_F titleRect = { 36.0f, 31.0f, 570.0f, 59.0f };
			const D2D1_RECT_F visualsRect = { 38.0f, 78.0f, 286.0f, 105.0f };
			const D2D1_RECT_F hudRect = { 318.0f, 78.0f, 566.0f, 105.0f };
			d2dContext->DrawText(title, _countof(title) - 1, titleFormat, titleRect, textBrush);
			d2dContext->DrawText(visuals, _countof(visuals) - 1, bodyFormat, visualsRect, textBrush);
			d2dContext->DrawText(hud, _countof(hud) - 1, bodyFormat, hudRect, textBrush);

			const auto drawToggle = [&](const D2D1_RECT_F& rect, bool enabled, bool hovered,
				const wchar_t* onText, const wchar_t* offText) {
				ID2D1SolidColorBrush* brush = enabled ? activeBrush : (hovered ? hoverBrush : headerBrush);
				d2dContext->FillRectangle(rect, brush);
				const wchar_t* text = enabled ? onText : offText;
				const D2D1_RECT_F textRect = { rect.left + 12.0f, rect.top + 8.0f, rect.right - 8.0f, rect.bottom - 4.0f };
				d2dContext->DrawText(text, static_cast<UINT32>(wcslen(text)), bodyFormat, textRect, textBrush);
			};

			drawToggle(crosshairButton, crosshairEnabled, hoverCrosshair, L"CROSSHAIR: ON", L"CROSSHAIR: OFF");
			drawToggle(watermarkButton, watermarkEnabled, hoverWatermark, L"WATERMARK: ON", L"WATERMARK: OFF");
			drawToggle(fpsButton, fpsEnabled, hoverFps, L"FPS COUNTER: ON", L"FPS COUNTER: OFF");
			drawToggle(moduleListButton, moduleListEnabled, hoverModuleList, L"MODULE LIST: ON", L"MODULE LIST: OFF");

			static const wchar_t note[] = L"Renderer-only modules - gameplay modules remain disabled";
			static const wchar_t footer[] = L"INSERT closes menu   |   CTRL+L unloads";
			const D2D1_RECT_F noteRect = { 38.0f, 270.0f, 570.0f, 292.0f };
			const D2D1_RECT_F footerRect = { 38.0f, 292.0f, 570.0f, 314.0f };
			d2dContext->DrawText(note, _countof(note) - 1, bodyFormat, noteRect, textBrush);
			d2dContext->DrawText(footer, _countof(footer) - 1, bodyFormat, footerRect, textBrush);
		}

		drawCrosshair(d2dTargets[index]);
		drawWatermark();
		drawFps();
		drawModuleList(d2dTargets[index]);

		const HRESULT drawHr = d2dContext->EndDraw();
		bridge11On12->ReleaseWrappedResources(&wrapped, 1);
		bridgeContext11->Flush();

		if (FAILED(drawHr)) {
			logF("[modern] Present Direct2D EndDraw failed hr=0x%08X; renderer will rebuild",
				static_cast<unsigned>(drawHr));
			releaseRenderer();
			return;
		}

		if (!loggedFirstDraw) {
			loggedFirstDraw = true;
			logF("[modern] Present Direct2D feature UI rendered successfully; Visuals + HUD modules ready");
		}
	}

	inline HRESULT __stdcall presentDetour(IDXGISwapChain* chain, UINT syncInterval, UINT flags) {
		auto original = presentHook->GetFastcall<HRESULT, IDXGISwapChain*, UINT, UINT>();

		++presentCount;
		tickFps();
		drawPresentOverlay(chain);

		const DWORD now = GetTickCount();
		if (!loggedPresent || presentCount <= 3 ||
			(Modern126Overlay::visible && (now - lastPresentLogTick) >= 2000)) {
			ID3D12Device* device12 = nullptr;
			ID3D11Device* device11 = nullptr;
			const HRESULT hr12 = chain != nullptr ? chain->GetDevice(__uuidof(ID3D12Device), reinterpret_cast<void**>(&device12)) : E_POINTER;
			const HRESULT hr11 = chain != nullptr ? chain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&device11)) : E_POINTER;

			const char* api = SUCCEEDED(hr12) && device12 != nullptr ? "DX12" :
				(SUCCEEDED(hr11) && device11 != nullptr ? "DX11" : "UNKNOWN");
			logF("[modern] DXGI Present #%llu chain=%llX api=%s queue=%llX menu=%s d2d=%s features=C%d/W%d/F%d/L%d",
				static_cast<unsigned long long>(presentCount),
				reinterpret_cast<uintptr_t>(chain), api,
				reinterpret_cast<uintptr_t>(capturedCommandQueue),
				Modern126Overlay::visible ? "ON" : "OFF",
				rendererReady ? "READY" : "WAIT",
				crosshairEnabled ? 1 : 0, watermarkEnabled ? 1 : 0,
				fpsEnabled ? 1 : 0, moduleListEnabled ? 1 : 0);
			loggedPresent = true;
			lastPresentLogTick = now;

			if (device12 != nullptr)
				device12->Release();
			if (device11 != nullptr)
				device11->Release();
		}

		return original(chain, syncInterval, flags);
	}

	inline void __stdcall executeCommandListsDetour(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists) {
		auto original = executeCommandListsHook->GetFastcall<void, ID3D12CommandQueue*, UINT, ID3D12CommandList* const*>();

		if (queue != nullptr && capturedCommandQueue == nullptr) {
			const D3D12_COMMAND_QUEUE_DESC desc = queue->GetDesc();
			if (desc.Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
				queue->AddRef();
				capturedCommandQueue = queue;
				if (!loggedQueue) {
					loggedQueue = true;
					logF("[modern] Captured Bedrock DX12 DIRECT command queue=%llX", reinterpret_cast<uintptr_t>(queue));
				}
			}
		}

		original(queue, count, lists);
	}

	inline bool createDummyWindow(HWND& window, HINSTANCE instance) {
		static const wchar_t* className = L"HorionModern126DxProbe";
		WNDCLASSEXW wc = {};
		wc.cbSize = sizeof(wc);
		wc.hInstance = instance;
		wc.lpfnWndProc = DefWindowProcW;
		wc.lpszClassName = className;

		if (RegisterClassExW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
			return false;

		window = CreateWindowExW(0, className, L"", WS_OVERLAPPED,
			0, 0, 64, 64, nullptr, nullptr, instance, nullptr);
		return window != nullptr;
	}

	inline bool start() {
		if (started)
			return true;

		HINSTANCE instance = GetModuleHandleW(nullptr);
		HWND window = nullptr;
		if (!createDummyWindow(window, instance)) {
			logF("[modern] DXGI Present probe failed: dummy window creation failed");
			return false;
		}

		DXGI_SWAP_CHAIN_DESC desc = {};
		desc.BufferCount = 2;
		desc.BufferDesc.Width = 64;
		desc.BufferDesc.Height = 64;
		desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		desc.OutputWindow = window;
		desc.SampleDesc.Count = 1;
		desc.Windowed = TRUE;
		desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

		IDXGISwapChain* dummySwapChain = nullptr;
		ID3D11Device* dummyDevice11 = nullptr;
		ID3D11DeviceContext* dummyContext11 = nullptr;
		D3D_FEATURE_LEVEL createdLevel = D3D_FEATURE_LEVEL_11_0;
		const D3D_FEATURE_LEVEL requestedLevels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1 };

		HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
			requestedLevels, _countof(requestedLevels), D3D11_SDK_VERSION, &desc,
			&dummySwapChain, &dummyDevice11, &createdLevel, &dummyContext11);

		if (FAILED(hr) || dummySwapChain == nullptr) {
			DestroyWindow(window);
			logF("[modern] DXGI Present probe failed: dummy D3D11 swap chain hr=0x%08X", static_cast<unsigned>(hr));
			if (dummyContext11 != nullptr) dummyContext11->Release();
			if (dummyDevice11 != nullptr) dummyDevice11->Release();
			return false;
		}

		auto* swapVtable = *reinterpret_cast<uintptr_t**>(dummySwapChain);
		const uintptr_t presentTarget = swapVtable != nullptr ? swapVtable[8] : 0;

		ID3D12Device* dummyDevice12 = nullptr;
		ID3D12CommandQueue* dummyQueue12 = nullptr;
		uintptr_t executeTarget = 0;
		if (SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dummyDevice12))) && dummyDevice12 != nullptr) {
			D3D12_COMMAND_QUEUE_DESC queueDesc = {};
			queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
			if (SUCCEEDED(dummyDevice12->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&dummyQueue12))) && dummyQueue12 != nullptr) {
				auto* queueVtable = *reinterpret_cast<uintptr_t**>(dummyQueue12);
				executeTarget = queueVtable != nullptr ? queueVtable[10] : 0;
			}
		}

		if (dummyQueue12 != nullptr) dummyQueue12->Release();
		if (dummyDevice12 != nullptr) dummyDevice12->Release();
		if (dummyContext11 != nullptr) dummyContext11->Release();
		if (dummyDevice11 != nullptr) dummyDevice11->Release();
		dummySwapChain->Release();
		DestroyWindow(window);

		if (presentTarget == 0) {
			logF("[modern] DXGI Present probe failed: Present target missing");
			return false;
		}

		presentHook = std::make_unique<FuncHook>(presentTarget, reinterpret_cast<void*>(presentDetour));
		presentHook->enableHook();

		if (executeTarget != 0) {
			executeCommandListsHook = std::make_unique<FuncHook>(executeTarget, reinterpret_cast<void*>(executeCommandListsDetour));
			executeCommandListsHook->enableHook();
		}

		started = true;
		logF("[modern] DXGI Present renderer installed Present=%llX ExecuteCommandLists=%llX",
			presentTarget, executeTarget);
		logF("[modern] Modern module layer registered: Visuals/Crosshair, HUD/Watermark, HUD/FPS Counter, HUD/Module List");
		return true;
	}

	inline void shutdown() {
		if (presentHook)
			presentHook->enableHook(false);
		if (executeCommandListsHook)
			executeCommandListsHook->enableHook(false);
		presentHook.reset();
		executeCommandListsHook.reset();
		releaseRenderer();
		releaseCapturedQueue();
		started = false;
	}
}
