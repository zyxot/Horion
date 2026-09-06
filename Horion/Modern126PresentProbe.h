#pragma once

#include <d3d11.h>
#include <d3d11on12.h>
#include <d3d12.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <dxgi.h>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dxgi.lib")

// Current Bedrock presents through DXGI/DX12. The maintained 1.26 client also
// renders its custom overlay from IDXGISwapChain::Present, using a D3D11On12
// bridge plus Direct2D/DirectWrite. This implementation mirrors only that
// renderer lifecycle: it draws a harmless diagnostic panel and text when the
// INSERT test toggle is active. No presentation flags or gameplay state are
// changed.
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
	inline uint64_t presentCount = 0;
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
	inline std::vector<ID3D11Resource*> wrappedTargets;
	inline std::vector<ID2D1Bitmap1*> d2dTargets;
	inline IDXGISwapChain* rendererChainIdentity = nullptr;
	inline bool rendererReady = false;

	template <typename T>
	inline void releaseCom(T*& value) {
		if (value != nullptr) {
			value->Release();
			value = nullptr;
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
		rendererReady = false;
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
		hr = d2dContext->CreateSolidColorBrush(panelColor, &panelBrush);
		if (FAILED(hr)) return failRenderer("panel brush", hr);
		hr = d2dContext->CreateSolidColorBrush(headerColor, &headerBrush);
		if (FAILED(hr)) return failRenderer("header brush", hr);
		hr = d2dContext->CreateSolidColorBrush(textColor, &textBrush);
		if (FAILED(hr)) return failRenderer("text brush", hr);

		DXGI_SWAP_CHAIN_DESC chainDesc = {};
		hr = chain->GetDesc(&chainDesc);
		if (FAILED(hr) || chainDesc.BufferCount == 0)
			return failRenderer("swap-chain description", hr);

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
			logF("[modern] Present Direct2D renderer READY chain=%llX buffers=%zu queue=%llX",
				reinterpret_cast<uintptr_t>(chain), d2dTargets.size(), reinterpret_cast<uintptr_t>(rendererQueue));
		}
		return true;
	}

	inline void drawPresentOverlay(IDXGISwapChain* chain) {
		if (!Modern126Overlay::visible || chain == nullptr || capturedCommandQueue == nullptr)
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

		ID3D11Resource* wrapped = wrappedTargets[index];
		bridge11On12->AcquireWrappedResources(&wrapped, 1);
		d2dContext->SetTarget(d2dTargets[index]);
		d2dContext->BeginDraw();

		const D2D1_RECT_F panel = { 24.0f, 24.0f, 414.0f, 196.0f };
		const D2D1_RECT_F header = { 24.0f, 24.0f, 414.0f, 62.0f };
		d2dContext->FillRectangle(panel, panelBrush);
		d2dContext->FillRectangle(header, headerBrush);

		static const wchar_t title[] = L"HORION PRESENT RENDERER TEST";
		static const wchar_t line1[] = L"Direct2D + DirectWrite on the DXGI backbuffer";
		static const wchar_t line2[] = L"This should stay visible every frame";
		static const wchar_t line3[] = L"INSERT toggles this panel";
		const D2D1_RECT_F titleRect = { 36.0f, 31.0f, 404.0f, 59.0f };
		const D2D1_RECT_F line1Rect = { 38.0f, 79.0f, 404.0f, 108.0f };
		const D2D1_RECT_F line2Rect = { 38.0f, 119.0f, 404.0f, 148.0f };
		const D2D1_RECT_F line3Rect = { 38.0f, 159.0f, 404.0f, 188.0f };

		d2dContext->DrawText(title, _countof(title) - 1, titleFormat, titleRect, textBrush);
		d2dContext->DrawText(line1, _countof(line1) - 1, bodyFormat, line1Rect, textBrush);
		d2dContext->DrawText(line2, _countof(line2) - 1, bodyFormat, line2Rect, textBrush);
		d2dContext->DrawText(line3, _countof(line3) - 1, bodyFormat, line3Rect, textBrush);

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
			logF("[modern] Present Direct2D text panel rendered successfully");
		}
	}

	inline HRESULT __stdcall presentDetour(IDXGISwapChain* chain, UINT syncInterval, UINT flags) {
		auto original = presentHook->GetFastcall<HRESULT, IDXGISwapChain*, UINT, UINT>();

		++presentCount;
		drawPresentOverlay(chain);

		const DWORD now = GetTickCount();
		if (!loggedPresent || presentCount <= 3 ||
			(Modern126Overlay::visible && (now - lastPresentLogTick) >= 1000)) {
			ID3D12Device* device12 = nullptr;
			ID3D11Device* device11 = nullptr;
			const HRESULT hr12 = chain != nullptr ? chain->GetDevice(__uuidof(ID3D12Device), reinterpret_cast<void**>(&device12)) : E_POINTER;
			const HRESULT hr11 = chain != nullptr ? chain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&device11)) : E_POINTER;

			const char* api = SUCCEEDED(hr12) && device12 != nullptr ? "DX12" :
				(SUCCEEDED(hr11) && device11 != nullptr ? "DX11" : "UNKNOWN");
			logF("[modern] DXGI Present #%llu chain=%llX api=%s queue=%llX menu=%s d2d=%s",
				static_cast<unsigned long long>(presentCount),
				reinterpret_cast<uintptr_t>(chain), api,
				reinterpret_cast<uintptr_t>(capturedCommandQueue),
				Modern126Overlay::visible ? "ON" : "OFF",
				rendererReady ? "READY" : "WAIT");
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