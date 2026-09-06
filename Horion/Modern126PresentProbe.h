#pragma once

#include <d3d11.h>
#include <d3d12.h>
#include <dxgi.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

// Minimal, pass-through DirectX probe for current Bedrock.
//
// The maintained 1.26 client renders its custom UI from IDXGISwapChain::Present
// rather than relying on MinecraftUIRenderContext text batching. This probe
// only proves that Horion can reach that renderer lifecycle safely. It does
// not modify presentation flags or issue any custom draw commands yet.
namespace Modern126PresentProbe {
	inline std::unique_ptr<FuncHook> presentHook;
	inline std::unique_ptr<FuncHook> executeCommandListsHook;
	inline ID3D12CommandQueue* capturedCommandQueue = nullptr;
	inline bool started = false;
	inline bool loggedPresent = false;
	inline bool loggedQueue = false;
	inline uint64_t presentCount = 0;

	inline void releaseCapturedQueue() {
		if (capturedCommandQueue != nullptr) {
			capturedCommandQueue->Release();
			capturedCommandQueue = nullptr;
		}
	}

	inline HRESULT __stdcall presentDetour(IDXGISwapChain* chain, UINT syncInterval, UINT flags) {
		auto original = presentHook->GetFastcall<HRESULT, IDXGISwapChain*, UINT, UINT>();

		++presentCount;
		if (!loggedPresent || presentCount <= 3 || Modern126Overlay::visible) {
			ID3D12Device* device12 = nullptr;
			ID3D11Device* device11 = nullptr;
			const HRESULT hr12 = chain != nullptr ? chain->GetDevice(__uuidof(ID3D12Device), reinterpret_cast<void**>(&device12)) : E_POINTER;
			const HRESULT hr11 = chain != nullptr ? chain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&device11)) : E_POINTER;

			const char* api = SUCCEEDED(hr12) && device12 != nullptr ? "DX12" :
				(SUCCEEDED(hr11) && device11 != nullptr ? "DX11" : "UNKNOWN");
			logF("[modern] DXGI Present #%llu chain=%llX api=%s queue=%llX menu=%s",
				static_cast<unsigned long long>(presentCount),
				reinterpret_cast<uintptr_t>(chain), api,
				reinterpret_cast<uintptr_t>(capturedCommandQueue),
				Modern126Overlay::visible ? "ON" : "OFF");
			loggedPresent = true;

			if (device12 != nullptr)
				device12->Release();
			if (device11 != nullptr)
				device11->Release();
		}

		return original(chain, syncInterval, flags);
	}

	inline void __stdcall executeCommandListsDetour(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists) {
		auto original = executeCommandListsHook->GetFastcall<void, ID3D12CommandQueue*, UINT, ID3D12CommandList* const*>();

		if (queue != nullptr) {
			const D3D12_COMMAND_QUEUE_DESC desc = queue->GetDesc();
			if (desc.Type == D3D12_COMMAND_LIST_TYPE_DIRECT && queue != capturedCommandQueue) {
				queue->AddRef();
				releaseCapturedQueue();
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
		logF("[modern] DXGI Present renderer probe installed Present=%llX ExecuteCommandLists=%llX",
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
		releaseCapturedQueue();
		started = false;
	}
}
