#include <Windows.h>
#include <cstdint>
#include <string>
#include <format>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cassert>
#include <dbghelp.h>
#include<dxgidebug.h>
#include<dxcapi.h>
#include"Matrix4x4.h"
#include"Vector3.h"

#ifdef USE_IMGUI
#include"externals/imgui/imgui.h"
#include"externals/imgui/imgui_impl_dx12.h"
#include"externals/imgui/imgui_impl_win32.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif
#include"externals/DirectXTex/DirectXTex.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "Dbghelp.lib")
#pragma comment(lib,"dxguid.lib")
#pragma comment(lib,"dxcompiler.lib")

namespace fs = std::filesystem;

std::string GetTimeStringForFile();

class Logger {
public:

	void Initialize() {

		// logsフォルダ作成
		fs::create_directories("logs");

		// ファイル名生成
		std::string filename =
			GetTimeStringForFile() + ".log";

		fs::path logFilePath =
			fs::path("logs") / filename;

		// ファイルを開きっぱなしにする
		logFile_.open(logFilePath, std::ios::app);
	}

	void Log(const std::string& message) {

		// デバッグ出力
		OutputDebugStringA((message + "\n").c_str());

		// ファイル出力
		if (logFile_.is_open()) {
			logFile_ << message << '\n';
		}
	}

private:
	std::ofstream logFile_;
};

struct Transform {
	Vector3 scale;
	Vector3 rotate;
	Vector3 translate;
};

//Tranceform変数を作る
Transform transform{ {1.0f,1.0f,1.0f},{0.0f,0.0f,0.0f,},{0.0f,0.0f,0.0f,} };

Transform cameraTransform{ {1.0f,1.0f,1.0f},{0.0f,0.0f,0.0f,},{0.0f,0.0f,-3.0f,} };

std::string GetTimeStringForFile() {
	auto now = std::chrono::system_clock::now();
	std::time_t t = std::chrono::system_clock::to_time_t(now);

	std::tm tm{};
	localtime_s(&tm, &t);

	std::ostringstream oss;
	oss << std::put_time(&tm, "%Y%m%d_%H%M%S");
	return oss.str();
}

std::string ConvertString(const std::wstring& str) {

	if (str.empty()) {
		return {};
	}

	int sizeNeeded = WideCharToMultiByte(
		CP_UTF8,
		0,
		str.data(),
		(int)str.size(),
		nullptr,
		0,
		nullptr,
		nullptr
	);

	std::string result(sizeNeeded, 0);

	WideCharToMultiByte(
		CP_UTF8,
		0,
		str.data(),
		(int)str.size(),
		result.data(),
		sizeNeeded,
		nullptr,
		nullptr
	);

	return result;
}

Logger logger;

IDxcBlob* CompileShader(
	const std::wstring& filePath,
	const wchar_t* profile,
	IDxcUtils* dxcUtils,
	IDxcCompiler3* dxcCompiler,
	IDxcIncludeHandler* includeHandler)
{
	logger.Log(ConvertString(
		std::format(L"Begin CompileShader, path:{}, profile:{}",
			filePath,
			profile)
	));

	// hlslファイルを読む
	IDxcBlobEncoding* shaderSource = nullptr;

	HRESULT hr = dxcUtils->LoadFile(
		filePath.c_str(),
		nullptr,
		&shaderSource
	);

	assert(SUCCEEDED(hr));

	// 読み込んだファイルの内容設定
	DxcBuffer shaderSourceBuffer;
	shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
	shaderSourceBuffer.Size = shaderSource->GetBufferSize();
	shaderSourceBuffer.Encoding = DXC_CP_UTF8;

	// Compileする
	LPCWSTR arguments[] = {
		filePath.c_str(),
		L"-E", L"main",
		L"-T", profile,
		L"-Zi",
		L"-Qembed_debug",
		L"-Od",
		L"-Zpr",
	};

	IDxcResult* shaderResult = nullptr;

	hr = dxcCompiler->Compile(
		&shaderSourceBuffer,
		arguments,
		_countof(arguments),
		includeHandler,
		IID_PPV_ARGS(&shaderResult)
	);

	assert(SUCCEEDED(hr));

	// Error確認
	IDxcBlobUtf8* shaderError = nullptr;

	shaderResult->GetOutput(
		DXC_OUT_ERRORS,
		IID_PPV_ARGS(&shaderError),
		nullptr
	);

	if (shaderError != nullptr &&
		shaderError->GetStringLength() != 0)
	{
		logger.Log(shaderError->GetStringPointer());

		assert(false);
	}

	// Compile結果取得
	IDxcBlob* shaderBlob = nullptr;

	hr = shaderResult->GetOutput(
		DXC_OUT_OBJECT,
		IID_PPV_ARGS(&shaderBlob),
		nullptr
	);

	assert(SUCCEEDED(hr));

	logger.Log("Compile Succeeded");

	// 解放
	shaderSource->Release();
	shaderResult->Release();

	return shaderBlob;
}

// ウィンドウプロシージャー
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {

#ifdef USE_IMGUI
	if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) {
		return true;
	}
#endif

	// メッセージに応じてゲーム固有の処理を行う
	switch (msg) {
		// ウィンドが破棄された
	case WM_DESTROY:
		// OSに対して、アプリの終了を伝える
		PostQuitMessage(0);
		return 0;
	}
	// 標準のメッセージ処理を行う
	return DefWindowProc(hwnd, msg, wparam, lparam);

}

ID3D12Device* device = nullptr;


LONG WINAPI ExportDump(EXCEPTION_POINTERS* exception)
{
	// dumpフォルダ作成
	fs::create_directories("dumps");

	// ファイル名生成
	std::string dumpName =
		"Crash_" + GetTimeStringForFile() + ".dmp";

	fs::path dumpPath =
		fs::path("dumps") / dumpName;

	HANDLE hFile = CreateFileA(
		dumpPath.string().c_str(),
		GENERIC_WRITE,
		0,
		nullptr,
		CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		nullptr
	);

	if (hFile != INVALID_HANDLE_VALUE)
	{
		MINIDUMP_EXCEPTION_INFORMATION dumpInfo{};
		dumpInfo.ThreadId = GetCurrentThreadId();
		dumpInfo.ExceptionPointers = exception;
		dumpInfo.ClientPointers = TRUE;

		MiniDumpWriteDump(
			GetCurrentProcess(),
			GetCurrentProcessId(),
			hFile,
			MiniDumpNormal,
			&dumpInfo,
			nullptr,
			nullptr
		);

		CloseHandle(hFile);
	}

	logger.Log("Crash dump generated.");

	return EXCEPTION_EXECUTE_HANDLER;
}

struct Vector4 {
	float x;
	float y;
	float z;
	float w;
};

struct Vector2 {
	float u;
	float v;
};

struct Material {
	Vector4 color;
	int32_t enableLighting;
};

struct TransformationMatrix {
	Matrix4x4 WVP;
	Matrix4x4 World;
};

struct DirectionalLight {
	Vector4 color;
	Vector3 direction;
	float intensity;
};

Matrix4x4 MakeIdentity4x4() {
	Matrix4x4 result{};

	result.m[0][0] = 1.0f;
	result.m[1][1] = 1.0f;
	result.m[2][2] = 1.0f;
	result.m[3][3] = 1.0f;

	return result;
}

ID3D12Resource* CreateBufferResource(
	ID3D12Device* d3dDevice,
	size_t sizeInBytes)
{
	// UploadHeap設定
	D3D12_HEAP_PROPERTIES uploadHeapProperties{};
	uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;

	// Resource設定
	D3D12_RESOURCE_DESC resourceDesc{};

	resourceDesc.Dimension =
		D3D12_RESOURCE_DIMENSION_BUFFER;

	resourceDesc.Width = static_cast<UINT64>(sizeInBytes);

	resourceDesc.Height = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;

	resourceDesc.SampleDesc.Count = 1;

	resourceDesc.Layout =
		D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	// Resource生成
	ID3D12Resource* resource = nullptr;

	HRESULT hr = d3dDevice->CreateCommittedResource(
		&uploadHeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&resource)
	);

	assert(SUCCEEDED(hr));

	return resource;
}

ID3D12Resource* CreateDepthStencilTextureResource(
	ID3D12Device* d3dDevice,
	int32_t width,
	int32_t height)
{
	// 1. 生成するResourceの設定
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Width = static_cast<UINT64>(width);       // Textureの幅
	resourceDesc.Height = static_cast<UINT>(height);       // Textureの高さ
	resourceDesc.MipLevels = 1;                            // mipmapの数
	resourceDesc.DepthOrArraySize = 1;                     // 奥行き or 配列Textureの配列数
	resourceDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;   // DepthStencilとして利用可能なフォーマット
	resourceDesc.SampleDesc.Count = 1;                     // サンプリングカウント。1固定。
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; // 2次元
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL; // DepthStencilとして使う通知

	// 2. 利用するHeapの設定
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;         // VRAM上に作る

	// 3. 深度値のクリア設定（最適化のために事前に値を指定しておく）
	D3D12_CLEAR_VALUE depthClearValue{};
	depthClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depthClearValue.DepthStencil.Depth = 1.0f;
	depthClearValue.DepthStencil.Stencil = 0;

	// 4. Resourceの生成
	ID3D12Resource* resource = nullptr;
	HRESULT hr = d3dDevice->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE, // 深度値を書き込む状態として作成
		&depthClearValue,
		IID_PPV_ARGS(&resource)
	);
	assert(SUCCEEDED(hr));

	return resource;
}

ID3D12DescriptorHeap* CreateDescriptorHeap(
	ID3D12Device* d3dDevice,
	D3D12_DESCRIPTOR_HEAP_TYPE heapType,
	UINT numDescriptors,
	bool shaderVisible)
{
	D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{};

	descriptorHeapDesc.Type = heapType;
	descriptorHeapDesc.NumDescriptors = numDescriptors;

	if (shaderVisible) {
		descriptorHeapDesc.Flags =
			D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	} else {
		descriptorHeapDesc.Flags =
			D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	}

	ID3D12DescriptorHeap* descriptorHeap = nullptr;

	HRESULT hr = d3dDevice->CreateDescriptorHeap(
		&descriptorHeapDesc,
		IID_PPV_ARGS(&descriptorHeap)
	);

	assert(SUCCEEDED(hr));

	return descriptorHeap;
}

// 特定のインデックスのCPUDescriptorHandleを取得する関数
D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index)
{
	D3D12_CPU_DESCRIPTOR_HANDLE handleCPU = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
	handleCPU.ptr += (descriptorSize * index);
	return handleCPU;
}

// 特定のインデックスのGPUDescriptorHandleを取得する関数
D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index)
{
	D3D12_GPU_DESCRIPTOR_HANDLE handleGPU = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
	handleGPU.ptr += (descriptorSize * index);
	return handleGPU;
}

// Windowsアプリでのエントリーポイント(main関数)
int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {
	CoInitializeEx(0, COINIT_MULTITHREADED);

	WNDCLASS wc{};
	// ウィンドウプロシージャ
	wc.lpfnWndProc = WindowProc;
	// ウィンドウクラス名(なんでも良い)
	wc.lpszClassName = L"CG2WindowClass";
	// インスタンスハンドル
	wc.hInstance = GetModuleHandle(nullptr);
	// カーソル
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

	// ウィンドウクラスを登録する
	RegisterClass(&wc);

	// クライアント領域サイズ
	const int32_t kClientWidth = 1280;
	const int32_t kClientHeight = 720;

	//　ウィンドウサイズを表す構造体にクライアント領域を入れる
	RECT wrc = { 0,0,kClientWidth,kClientHeight };

	// クライアント領域を元に実際のサイズにwrcを変更してもらう
	AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false);

	//ウィンドウの生成
	HWND hwnd = CreateWindow(
		wc.lpszClassName,       // 利用するクラス名
		L"CG2",                 // タイトルバーの文字(なんでも良い)
		WS_OVERLAPPEDWINDOW,     // よく見るウィンドウスタイル
		CW_USEDEFAULT,          // 表示X座標(Windowsに任せる)
		CW_USEDEFAULT,          // 表示Y座標(WindowsOSに任せる)
		wrc.right - wrc.left,  // ウィンドウ横幅
		wrc.bottom - wrc.top,   // ウィンドウ縦幅
		nullptr,                // 親ウィンドウハンドル
		nullptr,                // メニューハンドル
		wc.hInstance,           // インスタンスハンドル
		nullptr);               // オプション

	SetUnhandledExceptionFilter(ExportDump);

	logger.Initialize();

	logger.Log("ログ開始");

#ifdef _DEBUG
	ID3D12Debug1* debugController = nullptr;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
		//	デバックレイヤーを有効化する
		debugController->EnableDebugLayer();
		//さらにGPU側でもチェックを行うようにする
		debugController->SetEnableGPUBasedValidation(TRUE);
	}
#endif // DEBUG

	// 出力ウィンドへの文字出力
	logger.Log("Hello,DirectX!");

	// ========================
	// DXGIファクトリ生成
	// ========================
	IDXGIFactory7* dxgiFactory = nullptr;
	HRESULT hr = CreateDXGIFactory(IID_PPV_ARGS(&dxgiFactory));
	assert(SUCCEEDED(hr));

	// ========================
	// アダプタ選択
	// ========================
	IDXGIAdapter4* useAdapter = nullptr;

	for (UINT i = 0;; ++i) {

		IDXGIAdapter4* adapter = nullptr;

		hr = dxgiFactory->EnumAdapterByGpuPreference(
			i,
			DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
			IID_PPV_ARGS(&adapter)
		);

		if (hr == DXGI_ERROR_NOT_FOUND) {
			break;
		}

		assert(SUCCEEDED(hr));

		DXGI_ADAPTER_DESC3 adapterDesc{};
		hr = adapter->GetDesc3(&adapterDesc);
		assert(SUCCEEDED(hr));

		// ソフトウェアGPU除外
		if (adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE) {
			adapter->Release();
			continue;
		}

		std::wstring adapterMessage =
			L"Use Adapter: " + std::wstring(adapterDesc.Description);

		OutputDebugStringW((adapterMessage + L"\n").c_str());

		logger.Log(ConvertString(adapterMessage));

		// 採用
		useAdapter = adapter;
		break;
	}

	// 見つからなかったら落とす
	assert(useAdapter != nullptr);

	// ========================
	// デバイス生成
	// ========================

	// 機能レベル
	D3D_FEATURE_LEVEL featureLevels[] = {
		D3D_FEATURE_LEVEL_12_2,
		D3D_FEATURE_LEVEL_12_1,
		D3D_FEATURE_LEVEL_12_0
	};

	const char* featureLevelStrings[] = {
		"12.2", "12.1", "12.0"
	};

	for (size_t i = 0; i < _countof(featureLevels); ++i) {
		hr = D3D12CreateDevice(
			useAdapter,
			featureLevels[i],
			IID_PPV_ARGS(&device)
		);

		if (SUCCEEDED(hr)) {
			logger.Log(std::format("FeatureLevel : {}", featureLevelStrings[i]));
			break;
		}
	}

	// DescriptorSizeを取得しておく
	const uint32_t descriptorSizeSRV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	const uint32_t descriptorSizeRTV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	//const uint32_t descriptorSizeDSV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

	// 失敗したら即終了
	assert(device != nullptr);

	logger.Log("Complete create D3D12Device!!");

	//SRV用のヒープでディスクリプタの数は128。SRVはShaderないで触れるものなので、ShaderVisibleはtrue
	ID3D12DescriptorHeap* srvDescriptorHeap =
		CreateDescriptorHeap(
			device,
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
			128,
			true
		);

#ifdef _DEBUG
	ID3D12InfoQueue* infoQueue = nullptr;
	if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
		//やばいエラー時に止まる
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
		//エラー時に止まる
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
		//警告時に止まる
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);

		//抑制するメッセージのID
		D3D12_MESSAGE_ID denyIds[] = {
			//Windows11の中でのDXGIデバックレイヤーとのDX12デバックレイヤーの相互作用バグによるエラーメッセージ
			// https://stackoverflow.com/questions/69805245/directx-12-application-is-crashing-on-windows-11
			D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE
		};
		//抑制するレベル
		D3D12_MESSAGE_SEVERITY severities[] = { D3D12_MESSAGE_SEVERITY_INFO };
		D3D12_INFO_QUEUE_FILTER filter{};
		filter.DenyList.NumIDs = _countof(denyIds);
		filter.DenyList.pIDList = denyIds;
		filter.DenyList.NumSeverities = _countof(severities);
		filter.DenyList.pSeverityList = severities;
		//指定したメッセージの表示を抑制する
		infoQueue->PushStorageFilter(&filter);

		//解放
		infoQueue->Release();
	}
#endif // _DEBUG

	// コマンドキューを生成する
	// GPUへコマンドを送信するためのキュー
	// CommandListはここ経由でGPU実行される
	ID3D12CommandQueue* commandQueue = nullptr;
	D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};

	hr = device->CreateCommandQueue(
		&commandQueueDesc,
		IID_PPV_ARGS(&commandQueue)
	);

	assert(SUCCEEDED(hr));

	// コマンドアロケータを生成する
	// GPUコマンドを記録するためのメモリ管理オブジェクト
	// CommandListとセットで使用する
	ID3D12CommandAllocator* commandAllocator = nullptr;

	hr = device->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(&commandAllocator)
	);

	assert(SUCCEEDED(hr));

	// コマンドリストを生成する
	// GPUへ送る描画命令を記録するリスト
	// DrawやBarrierなどの命令をここへ積む
	ID3D12GraphicsCommandList* commandList = nullptr;

	hr = device->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		commandAllocator,
		nullptr,
		IID_PPV_ARGS(&commandList)
	);

	assert(SUCCEEDED(hr));

	// ========================
	// Fence生成
	// ========================

	// GPU同期用Fence
	// CPUがGPUの処理完了を待つために使う
	ID3D12Fence* fence = nullptr;

	// Fenceの現在値
	// Signalするたびに増やす
	uint64_t fenceValue = 0;

	// Fenceオブジェクト生成
	// CPUとGPUの同期に使用する
	hr = device->CreateFence(
		fenceValue,
		D3D12_FENCE_FLAG_NONE,
		IID_PPV_ARGS(&fence)
	);

	assert(SUCCEEDED(hr));

	// Fence待機用Event
	HANDLE fenceEvent = CreateEvent(
		nullptr,
		FALSE,
		FALSE,
		nullptr
	);

	assert(fenceEvent != nullptr);

	// ========================
	// DXC初期化
	// ========================

	// dxcUtils
	IDxcUtils* dxcUtils = nullptr;

	hr = DxcCreateInstance(
		CLSID_DxcUtils,
		IID_PPV_ARGS(&dxcUtils)
	);

	assert(SUCCEEDED(hr));

	// dxcCompiler
	IDxcCompiler3* dxcCompiler = nullptr;

	hr = DxcCreateInstance(
		CLSID_DxcCompiler,
		IID_PPV_ARGS(&dxcCompiler)
	);

	assert(SUCCEEDED(hr));

	// includeHandler
	IDxcIncludeHandler* includeHandler = nullptr;

	hr = dxcUtils->CreateDefaultIncludeHandler(
		&includeHandler
	);

	assert(SUCCEEDED(hr));

	// ========================
	// RootSignature
	// ========================

	D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};

	descriptionRootSignature.Flags =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	D3D12_ROOT_PARAMETER rootParameters[4]{};

	rootParameters[0].ParameterType =
		D3D12_ROOT_PARAMETER_TYPE_CBV;    //CBVを使う

	rootParameters[0].Descriptor.ShaderRegister = 0;  //レジスタ番号0を使う

	rootParameters[0].ShaderVisibility =
		D3D12_SHADER_VISIBILITY_PIXEL;    //PixelShaderで使う

	rootParameters[1].ParameterType =
		D3D12_ROOT_PARAMETER_TYPE_CBV;    //CBVを使う

	rootParameters[1].Descriptor.ShaderRegister = 0;  //レジスタ番号0を使う

	rootParameters[1].ShaderVisibility =
		D3D12_SHADER_VISIBILITY_VERTEX;    //VertexShaderで使う

	D3D12_DESCRIPTOR_RANGE descriptorRange[1]{};
	descriptorRange[0].BaseShaderRegister = 0; // レジスタ番号0
	descriptorRange[0].NumDescriptors = 1; // 1つのテクスチャ
	descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; // SRVを使う
	descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; // DescriptorTableを使う
	rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // PixelShaderで使う
	rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRange;
	rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange);

	descriptionRootSignature.pParameters = rootParameters;    //ルートパラメータ配列へのポインタ
	descriptionRootSignature.NumParameters = _countof(rootParameters);    //配列の長さ

	rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[3].Descriptor.ShaderRegister = 1;

	D3D12_STATIC_SAMPLER_DESC staticSamplers[1]{};
	staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR; // バイリニア
	staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP; // リピート
	staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX; // ミップマップ最大
	staticSamplers[0].ShaderRegister = 0; // レジスタ番号0
	staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // PixelShaderで使う
	descriptionRootSignature.pStaticSamplers = staticSamplers;
	descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers);

	ID3DBlob* signatureBlob = nullptr;
	ID3DBlob* errorBlob = nullptr;

	hr = D3D12SerializeRootSignature(
		&descriptionRootSignature,
		D3D_ROOT_SIGNATURE_VERSION_1,
		&signatureBlob,
		&errorBlob
	);

	if (FAILED(hr)) {
		logger.Log((char*)errorBlob->GetBufferPointer());
		assert(false);
	}

	ID3D12RootSignature* rootSignature = nullptr;

	hr = device->CreateRootSignature(
		0,
		signatureBlob->GetBufferPointer(),
		signatureBlob->GetBufferSize(),
		IID_PPV_ARGS(&rootSignature)
	);

	assert(SUCCEEDED(hr));

	// ========================
	// InputLayout
	// ========================
	// 要素数を2に増やします
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[3]{};

	// 0番目: POSITION
	inputElementDescs[0].SemanticName = "POSITION";
	inputElementDescs[0].SemanticIndex = 0;
	inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	// 1番目: TEXCOORD
	inputElementDescs[1].SemanticName = "TEXCOORD";
	inputElementDescs[1].SemanticIndex = 0;
	inputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT;
	inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	//3番目: NORMAL
	inputElementDescs[2].SemanticName = "NORMAL";
	inputElementDescs[2].SemanticIndex = 0;
	inputElementDescs[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	inputElementDescs[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
	inputLayoutDesc.pInputElementDescs = inputElementDescs;
	inputLayoutDesc.NumElements = _countof(inputElementDescs);


	// ========================
	// BlendState
	// ========================

	D3D12_BLEND_DESC blendDesc{};
	//	全ての色要素を書き込む
	blendDesc.RenderTarget[0].RenderTargetWriteMask =
		D3D12_COLOR_WRITE_ENABLE_ALL;

	// ========================
	// RasterizerState
	// ========================

	D3D12_RASTERIZER_DESC rasterizerDesc{};

	rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

	// ========================
	// Shader Compile
	// ========================

	IDxcBlob* vertexShaderBlob = CompileShader(
		L"Object3d.VS.hlsl",
		L"vs_6_0",
		dxcUtils,
		dxcCompiler,
		includeHandler
	);

	IDxcBlob* pixelShaderBlob = CompileShader(
		L"Object3d.PS.hlsl",
		L"ps_6_0",
		dxcUtils,
		dxcCompiler,
		includeHandler
	);

	// ========================
	// PSO
	// ========================

	D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc{};

	graphicsPipelineStateDesc.pRootSignature = rootSignature;

	graphicsPipelineStateDesc.InputLayout = inputLayoutDesc;

	graphicsPipelineStateDesc.VS.pShaderBytecode =
		vertexShaderBlob->GetBufferPointer();

	graphicsPipelineStateDesc.VS.BytecodeLength =
		vertexShaderBlob->GetBufferSize();

	graphicsPipelineStateDesc.PS.pShaderBytecode =
		pixelShaderBlob->GetBufferPointer();

	graphicsPipelineStateDesc.PS.BytecodeLength =
		pixelShaderBlob->GetBufferSize();

	graphicsPipelineStateDesc.BlendState = blendDesc;

	graphicsPipelineStateDesc.RasterizerState = rasterizerDesc;

	graphicsPipelineStateDesc.NumRenderTargets = 1;

	graphicsPipelineStateDesc.RTVFormats[0] =
		DXGI_FORMAT_R8G8B8A8_UNORM;

	graphicsPipelineStateDesc.PrimitiveTopologyType =
		D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	graphicsPipelineStateDesc.SampleDesc.Count = 1;

	graphicsPipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

	graphicsPipelineStateDesc.RasterizerState.CullMode =
		D3D12_CULL_MODE_BACK;

	graphicsPipelineStateDesc.DepthStencilState.DepthEnable = TRUE; // 深度テストを行う
	graphicsPipelineStateDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL; // 深度値を書き込む
	graphicsPipelineStateDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS; // 手前にあるものを描画
	graphicsPipelineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT; // 深度バッファのフォーマット指定

	ID3D12PipelineState* graphicsPipelineState = nullptr;

	hr = device->CreateGraphicsPipelineState(
		&graphicsPipelineStateDesc,
		IID_PPV_ARGS(&graphicsPipelineState)
	);

	assert(SUCCEEDED(hr));

	// ========================
	// VertexResource (球体データの設定)
	// ========================

	// 頂点構造体の定義
	struct VertexData {
		Vector4 position;
		Vector2 texcoord;
		Vector3 normal;
	};

	const uint32_t kSubdivision = 16;
	const uint32_t kVertexCountSphere = kSubdivision * kSubdivision * 6; // 16 * 16 * 6 = 1536

	// 頂点データを格納する動的配列（vector）を用意
	std::vector<VertexData> sphereVertices(kVertexCountSphere);

	// 角度の増分を計算 (スライドの仕様通り)
	const float pi = 3.1415926535f;
	const float kLonEvery = pi * 2.0f / static_cast<float>(kSubdivision); // 経度分割1つ分の角度
	const float kLatEvery = pi / static_cast<float>(kSubdivision);        // 緯度分割1つ分の角度

	// 緯度方向に分割してループ
	for (uint32_t latIndex = 0; latIndex < kSubdivision; ++latIndex) {
		// 緯度の角度 θ (南極 -pi/2 から 北極 pi/2 まで進む)
		float lat = -pi / 2.0f + kLatEvery * static_cast<float>(latIndex);

		// 経度方向に分割してループしながら面（2枚の三角形）を構築
		for (uint32_t lonIndex = 0; lonIndex < kSubdivision; ++lonIndex) {
			// 経度の角度 φ
			float lon = static_cast<float>(lonIndex) * kLonEvery;

			// この四角形（a,b,c,d）の書き込み開始インデックスを計算
			uint32_t start = (latIndex * kSubdivision + lonIndex) * 6;

			// 各増分角度
			float nextLat = lat + kLatEvery;
			float nextLon = lon + kLonEvery;

			// 頂点位置の計算
			Vector4 posA = { cosf(lat) * cosf(lon), sinf(lat), cosf(lat) * sinf(lon), 1.0f };
			Vector4 posB = { cosf(nextLat) * cosf(lon), sinf(nextLat), cosf(nextLat) * sinf(lon), 1.0f };
			Vector4 posC = { cosf(lat) * cosf(nextLon), sinf(lat), cosf(lat) * sinf(nextLon), 1.0f };
			Vector4 posD = { cosf(nextLat) * cosf(nextLon), sinf(nextLat), cosf(nextLat) * sinf(nextLon), 1.0f };

			// Texcoord(UV)の計算
			float uA = static_cast<float>(lonIndex) / static_cast<float>(kSubdivision);
			float vA = 1.0f - static_cast<float>(latIndex) / static_cast<float>(kSubdivision);

			float uB = uA;
			float vB = 1.0f - static_cast<float>(latIndex + 1) / static_cast<float>(kSubdivision);

			float uC = static_cast<float>(lonIndex + 1) / static_cast<float>(kSubdivision);
			float vC = vA;

			float uD = uC;
			float vD = vB;

			// 6つの頂点データに書き込み（法線情報を追加）
			sphereVertices[start + 0] = { posA, { uA, vA }, { posA.x, posA.y, posA.z } };
			sphereVertices[start + 1] = { posB, { uB, vB }, { posB.x, posB.y, posB.z } };
			sphereVertices[start + 2] = { posC, { uC, vC }, { posC.x, posC.y, posC.z } };

			sphereVertices[start + 3] = { posB, { uB, vB }, { posB.x, posB.y, posB.z } };
			sphereVertices[start + 4] = { posD, { uD, vD }, { posD.x, posD.y, posD.z } };
			sphereVertices[start + 5] = { posC, { uC, vC }, { posC.x, posC.y, posC.z } };
		}
	}

	OutputDebugStringA(("C++ Size: " + std::to_string(sizeof(TransformationMatrix))).c_str());

	// 1. GPU上に球体用のバッファを確保
	ID3D12Resource* vertexResource =
		CreateBufferResource(
			device,
			sizeof(VertexData) * kVertexCountSphere
		);

	// 2. CPU側の計算結果(sphereVertices)をGPUメモリ(vertexData)にコピー
	VertexData* vertexData = nullptr;
	vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));

	memcpy(vertexData, sphereVertices.data(), sizeof(VertexData) * kVertexCountSphere);

	vertexResource->Unmap(0, nullptr);

	// 3. VertexBufferView（VBV）の設定
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
	vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();
	vertexBufferView.SizeInBytes = sizeof(VertexData) * kVertexCountSphere;
	vertexBufferView.StrideInBytes = sizeof(VertexData);

	// Material用Resource
	ID3D12Resource* materialResource =
		CreateBufferResource(
			device,
			(sizeof(Material) + 0xff) & ~0xff
		);

	// TransformationMatrix用のリソースを作る（サイズを拡張）
	ID3D12Resource* wvpResource =
		CreateBufferResource(
			device,256 // ←構造体サイズに変更
		);

	// データを書き込む（ポインタの型を TransformationMatrix* にする）
	TransformationMatrix* wvpData = nullptr;
	// 書き込むためのアドレスを取得
	wvpResource->Map(0, nullptr, reinterpret_cast<void**>(&wvpData));
	// 各メンバに単位行列を書き込んでおく
	wvpData->WVP = MakeIdentity4x4();
	wvpData->World = MakeIdentity4x4();

	// ========================
	// Materialデータを書き込む
	// ========================

	Material* materialData = nullptr;

	materialResource->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&materialData)
	);

	// 色設定
	materialData->color = {
	1.0f,
	1.0f,
	1.0f,
	1.0f
	};
	materialData->enableLighting = true;


	// Sprite用の頂点リソースを作る (6頂点分)
	ID3D12Resource* vertexResourceSprite = CreateBufferResource(device, sizeof(VertexData) * 6);

	// 頂点バッファビューを作成する
	D3D12_VERTEX_BUFFER_VIEW vertexBufferViewSprite{};
	// リソースの先頭アドレスから使う
	vertexBufferViewSprite.BufferLocation = vertexResourceSprite->GetGPUVirtualAddress();
	// 使用するリソースのサイズは頂点6つ分のサイズ
	vertexBufferViewSprite.SizeInBytes = sizeof(VertexData) * 6;
	// 1頂点あたりのサイズ
	vertexBufferViewSprite.StrideInBytes = sizeof(VertexData);

	VertexData* vertexDataSprite = nullptr;
	vertexResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&vertexDataSprite));

	// 1枚目の三角形
	vertexDataSprite[0].position = { 0.0f, 360.0f, 0.0f, 1.0f }; // 左下
	vertexDataSprite[0].texcoord = { 0.0f, 1.0f };
	vertexDataSprite[0].normal = { 0.0f, 0.0f, -1.0f };
	vertexDataSprite[1].position = { 0.0f, 0.0f, 0.0f, 1.0f };   // 左上
	vertexDataSprite[1].texcoord = { 0.0f, 0.0f };
	vertexDataSprite[1].normal = { 0.0f, 0.0f, -1.0f };
	vertexDataSprite[2].position = { 640.0f, 360.0f, 0.0f, 1.0f }; // 右下
	vertexDataSprite[2].texcoord = { 1.0f, 1.0f };
	vertexDataSprite[2].normal = { 0.0f, 0.0f, -1.0f };

	// 2枚目の三角形
	vertexDataSprite[3].position = { 0.0f, 0.0f, 0.0f, 1.0f };   // 左上
	vertexDataSprite[3].texcoord = { 0.0f, 0.0f };
	vertexDataSprite[3].normal = { 0.0f, 0.0f, -1.0f };
	vertexDataSprite[4].position = { 640.0f, 0.0f, 0.0f, 1.0f };  // 右上
	vertexDataSprite[4].texcoord = { 1.0f, 0.0f };
	vertexDataSprite[4].normal = { 0.0f, 0.0f, -1.0f };
	vertexDataSprite[5].position = { 640.0f, 360.0f, 0.0f, 1.0f }; // 右下
	vertexDataSprite[5].texcoord = { 1.0f, 1.0f };
	vertexDataSprite[5].normal = { 0.0f, 0.0f, -1.0f };


	ID3D12Resource* indexResourceSprite = CreateBufferResource(device, sizeof(uint32_t) * 6);

	// データの書き込み
	uint32_t* indexDataSprite = nullptr;
	indexResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&indexDataSprite));
	indexDataSprite[0] = 0; indexDataSprite[1] = 1; indexDataSprite[2] = 2;
	indexDataSprite[3] = 1; indexDataSprite[4] = 3; indexDataSprite[5] = 2;
	indexResourceSprite->Unmap(0, nullptr);

	// --- IndexBufferViewの作成 ---
	D3D12_INDEX_BUFFER_VIEW indexBufferViewSprite{};
	indexBufferViewSprite.BufferLocation = indexResourceSprite->GetGPUVirtualAddress();
	indexBufferViewSprite.SizeInBytes = sizeof(uint32_t) * 6;
	indexBufferViewSprite.Format = DXGI_FORMAT_R32_UINT;



	// Sprite用のマテリアルリソースを作る
	//ID3D12Resource* materialResourceSprite =
	//	CreateBufferResource(device, (sizeof(Material) + 0xff) & ~0xff);

	ID3D12Resource* directionalLightResource = CreateBufferResource(device, sizeof(DirectionalLight));
	DirectionalLight* directionalLightData = nullptr;
	directionalLightResource->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData));

	// 色は白を設定しておく
	// デフォルト値の設定
	directionalLightData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	directionalLightData->direction = { 0.0f, -1.0f, 0.0f };
	directionalLightData->intensity = 1.0f;

	// ========================
	// Textureデータの読み込みと転送
	// ========================

	// 1. Textureファイルをロード (DirectXTexを使用)
	DirectX::ScratchImage mipImages{};
	hr = DirectX::LoadFromWICFile(
		L"resources/uvChecker.png",
		DirectX::WIC_FLAGS_NONE,
		nullptr,
		mipImages
	);
	assert(SUCCEEDED(hr));

	// メタデータの取得
	const DirectX::TexMetadata& metadata = mipImages.GetMetadata();

	// 2. TextureResourceの設定
	D3D12_RESOURCE_DESC textureDesc{};
	textureDesc.Width = static_cast<UINT64>(metadata.width);          // 横幅
	textureDesc.Height = static_cast<UINT>(metadata.height);          // 縦幅
	textureDesc.MipLevels = static_cast<UINT16>(metadata.mipLevels);  // ミップマップの数
	textureDesc.DepthOrArraySize = static_cast<UINT16>(metadata.arraySize); // 奥行き or 配列数
	textureDesc.Format = metadata.format;                             // フォーマット
	textureDesc.SampleDesc.Count = 1;                                 // マルチサンプルは1
	textureDesc.Dimension = static_cast<D3D12_RESOURCE_DIMENSION>(metadata.dimension); // 二次元テクスチャ

	// Heapの設定 (CPUPagePropertyにWRITE_BACKを指定して直接書き込める特殊なカスタムヒープ)
	D3D12_HEAP_PROPERTIES textureHeapProperties{};
	textureHeapProperties.Type = D3D12_HEAP_TYPE_CUSTOM;
	textureHeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
	textureHeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;

	// テクスチャ用リソースの生成
	ID3D12Resource* textureResource = nullptr;
	hr = device->CreateCommittedResource(
		&textureHeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&textureDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, // 最初からシェーダで読み込める状態で作成
		nullptr,
		IID_PPV_ARGS(&textureResource)
	);
	assert(SUCCEEDED(hr));

	// 3. データをGPUリソース（サブリソース）に直接転送する
	for (size_t mipLevel = 0; mipLevel < metadata.mipLevels; ++mipLevel) {
		// ミップマップレベルごとの画像イメージを取得
		const DirectX::Image* img = mipImages.GetImage(mipLevel, 0, 0);

		// 書き込み
		hr = textureResource->WriteToSubresource(
			static_cast<UINT>(mipLevel),
			nullptr,                            // 全領域コピーのためnull
			img->pixels,                        // 元データ
			static_cast<UINT>(img->rowPitch),   // 1ラインあたりのバイト数
			static_cast<UINT>(img->slicePitch)  // 1枚あたりのバイト数
		);
		assert(SUCCEEDED(hr));
	}

	// ========================
	// SRVの作成
	// ========================

	// metadataをもとにSRVの設定を行う
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = metadata.format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; // 2Dテクスチャ
	srvDesc.Texture2D.MipLevels = static_cast<UINT>(metadata.mipLevels);

	// SRVのハンドル（場所）を決定する
	// 0番目はImGuiが使っているので、関数を使ってスマートに「1番目」を取得！
	D3D12_CPU_DESCRIPTOR_HANDLE srvHandleCPU = GetCPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, 1);
	D3D12_GPU_DESCRIPTOR_HANDLE srvHandleGPU = GetGPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, 1);

	// SRVの生成
	device->CreateShaderResourceView(textureResource, &srvDesc, srvHandleCPU);

	assert(SUCCEEDED(hr));

	// ========================
	// サンプラーの作成
	// ========================
	D3D12_SAMPLER_DESC samplerDesc{};
	samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR; // バイリニアフィルタ
	samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP; // リピート
	samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;

	// サンプラー用デスクリプタヒープからハンドルを取得
	ID3D12DescriptorHeap* samplerDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1, false);
	D3D12_CPU_DESCRIPTOR_HANDLE samplerHandleCPU = samplerDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

	// サンプラーの生成
	device->CreateSampler(&samplerDesc, samplerHandleCPU);

	// スワップチェーンを生成する
	// 画面表示用のSwapChain
	// BackBufferを切り替えながら描画する
	IDXGISwapChain4* swapChain = nullptr;

	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
	swapChainDesc.Width = kClientWidth;
	swapChainDesc.Height = kClientHeight;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = 2;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

	hr = dxgiFactory->CreateSwapChainForHwnd(
		commandQueue,
		hwnd,
		&swapChainDesc,
		nullptr,
		nullptr,
		reinterpret_cast<IDXGISwapChain1**>(&swapChain)
	);

	assert(SUCCEEDED(hr));

	// RTV用DescriptorHeap
	// RTV(RenderTargetView)を格納するヒープ
	// GPUリソースの参照情報を保持する
	ID3D12DescriptorHeap* rtvDescriptorHeap = nullptr;

	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.NumDescriptors = 2;

	hr = device->CreateDescriptorHeap(
		&rtvHeapDesc,
		IID_PPV_ARGS(&rtvDescriptorHeap)
	);

	assert(SUCCEEDED(hr));

	// DSV用のDescriptorHeapを作成
	ID3D12DescriptorHeap* dsvDescriptorHeap = CreateDescriptorHeap(
		device,
		D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
		1,      // 深度バッファは1つ
		false   // シェーダからは直接見えない
	);

	// 深度バッファリソース（Texture）の生成
	ID3D12Resource* depthStencilResource = CreateDepthStencilTextureResource(
		device,
		kClientWidth,
		kClientHeight
	);

	// DSVの作成
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; // フォーマット
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D; // 2Dテクスチャとして扱う

	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	device->CreateDepthStencilView(depthStencilResource, &dsvDesc, dsvHandle);

	// SwapChainのリソース
	ID3D12Resource* swapChainResources[2] = {};

	hr = swapChain->GetBuffer(0, IID_PPV_ARGS(&swapChainResources[0]));
	assert(SUCCEEDED(hr));

	hr = swapChain->GetBuffer(1, IID_PPV_ARGS(&swapChainResources[1]));
	assert(SUCCEEDED(hr));

	// RTV作成
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2];
	rtvHandles[0] = GetCPUDescriptorHandle(rtvDescriptorHeap, descriptorSizeRTV, 0);
	rtvHandles[1] = GetCPUDescriptorHandle(rtvDescriptorHeap, descriptorSizeRTV, 1);

	device->CreateRenderTargetView(swapChainResources[0], &rtvDesc, rtvHandles[0]);
	device->CreateRenderTargetView(swapChainResources[1], &rtvDesc, rtvHandles[1]);

#ifdef USE_IMGUI
	// ========================
	// ImGui初期化
	// ========================
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();

	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX12_Init(
		device,
		swapChainDesc.BufferCount,
		rtvDesc.Format,
		srvDescriptorHeap,
		srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart()
	);

	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->Build();
#endif

	// Sprite用のTransformationMatrix用のリソースを作る
	ID3D12Resource* transformationMatrixResourceSprite = CreateBufferResource(device, sizeof(Matrix4x4));

	// データを書き込む
	Matrix4x4* transformationMatrixDataSprite = nullptr;
	transformationMatrixResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixDataSprite));
	*transformationMatrixDataSprite = MakeIdentity4x4();

	// CPUで動かす用のTransformを作る
	Transform transformSprite{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };

	// 2枚目のTextureの読み込みとSRV追加
	DirectX::ScratchImage mipImages2{};
	hr = DirectX::LoadFromWICFile(
		L"resources/monsterBall.png",
		DirectX::WIC_FLAGS_NONE,
		nullptr,
		mipImages2
	);
	assert(SUCCEEDED(hr));

	const DirectX::TexMetadata& metadata2 = mipImages2.GetMetadata();

	D3D12_RESOURCE_DESC textureDesc2{};
	textureDesc2.Width = static_cast<UINT64>(metadata2.width);
	textureDesc2.Height = static_cast<UINT>(metadata2.height);
	textureDesc2.MipLevels = static_cast<UINT16>(metadata2.mipLevels);
	textureDesc2.DepthOrArraySize = static_cast<UINT16>(metadata2.arraySize);
	textureDesc2.Format = metadata2.format;
	textureDesc2.SampleDesc.Count = 1;
	textureDesc2.Dimension = static_cast<D3D12_RESOURCE_DIMENSION>(metadata2.dimension);

	D3D12_HEAP_PROPERTIES textureHeapProperties2{};
	textureHeapProperties2.Type = D3D12_HEAP_TYPE_CUSTOM;
	textureHeapProperties2.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
	textureHeapProperties2.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;

	ID3D12Resource* textureResource2 = nullptr;
	hr = device->CreateCommittedResource(
		&textureHeapProperties2,
		D3D12_HEAP_FLAG_NONE,
		&textureDesc2,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&textureResource2)
	);
	assert(SUCCEEDED(hr));

	for (size_t mipLevel = 0; mipLevel < metadata2.mipLevels; ++mipLevel) {
		const DirectX::Image* img = mipImages2.GetImage(mipLevel, 0, 0);
		hr = textureResource2->WriteToSubresource(
			static_cast<UINT>(mipLevel),
			nullptr,
			img->pixels,
			static_cast<UINT>(img->rowPitch),
			static_cast<UINT>(img->slicePitch)
		);
		assert(SUCCEEDED(hr));
	}

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc2{};
	srvDesc2.Format = metadata2.format;
	srvDesc2.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc2.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc2.Texture2D.MipLevels = static_cast<UINT>(metadata2.mipLevels);

	// インデックス 2 の位置に作成
	D3D12_CPU_DESCRIPTOR_HANDLE srvHandleCPU2 = GetCPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, 2);
	D3D12_GPU_DESCRIPTOR_HANDLE srvHandleGPU2 = GetGPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, 2);

	device->CreateShaderResourceView(textureResource2, &srvDesc2, srvHandleCPU2);

	// SRVを切り替えるのフラグを追加
	bool useMonsterBall = true;

	// ウィンドウを表示する
	ShowWindow(hwnd, SW_SHOW);

	MSG msg{};
	while (msg.message != WM_QUIT) {
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		} else {
			// ゲーム処理

#ifdef USE_IMGUI
			ImGui_ImplDX12_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();
			ImGui::ShowDemoWindow();

			ImGui::Begin("Debug Window");
			ImGui::Checkbox("useMonsterBall", &useMonsterBall);
			ImGui::Separator();

			if (ImGui::TreeNode("3D Sphere")) {
				ImGui::SliderFloat3("Translation", &transform.translate.x, -10.0f, 10.0f);
				ImGui::SliderFloat3("Rotation", &transform.rotate.x, -3.14f, 3.14f);
				ImGui::SliderFloat3("Scale", &transform.scale.x, 0.1f, 5.0f);
				ImGui::TreePop();
			}

			ImGui::Separator();

			if (ImGui::TreeNode("2D Sprite")) {
				ImGui::SliderFloat3("Position", &transformSprite.translate.x, 0.0f, (float)kClientWidth);
				ImGui::SliderFloat3("Rotation", &transformSprite.rotate.x, -3.14f, 3.14f);
				ImGui::SliderFloat2("Scale (Px)", &transformSprite.scale.x, 1.0f, 1000.0f);
				ImGui::TreePop();
			}
			ImGui::End();
#endif

			transform.rotate.y += 0.03f;
			Matrix4x4 worldMatrix = Matrix4x4::MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
			Matrix4x4 cameraMatrix = Matrix4x4::MakeAffineMatrix(cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate);
			Matrix4x4 viewMatrix = Matrix4x4::Inverse(cameraMatrix);
			Matrix4x4 projectionMatrix = Matrix4x4::MakePerspectiveFovMatrix(0.45f, float(kClientWidth) / float(kClientHeight), 0.01f, 100.0f);

			// 拡張した構造体の各メンバにそれぞれ代入する
			wvpData->WVP = Matrix4x4::Multiply(worldMatrix, Matrix4x4::Multiply(viewMatrix, projectionMatrix));
			wvpData->World = worldMatrix;

			Matrix4x4 worldMatrixSprite = Matrix4x4::MakeAffineMatrix(transformSprite.scale, transformSprite.rotate, transformSprite.translate);
			Matrix4x4 viewMatrixSprite = MakeIdentity4x4();
			Matrix4x4 projectionMatrixSprite = Matrix4x4::MakeOrthographicMatrix(0.0f, 0.0f, float(kClientWidth), float(kClientHeight), 0.0f, 100.0f);
			*transformationMatrixDataSprite = Matrix4x4::Multiply(worldMatrixSprite, Matrix4x4::Multiply(viewMatrixSprite, projectionMatrixSprite));

#ifdef USE_IMGUI
			ImGui::Render();
#endif
			UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();

			D3D12_RESOURCE_BARRIER barrier{};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			barrier.Transition.pResource = swapChainResources[backBufferIndex];
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
			barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			commandList->ResourceBarrier(1, &barrier);

			D3D12_VIEWPORT viewport{ 0.0f, 0.0f, static_cast<float>(kClientWidth), static_cast<float>(kClientHeight), 0.0f, 1.0f };
			commandList->RSSetViewports(1, &viewport);

			D3D12_RECT scissorRect{ 0, 0, kClientWidth, kClientHeight };
			commandList->RSSetScissorRects(1, &scissorRect);

			float clearColor[] = { 0.2f, 0.6f, 0.9f, 1.0f };

			dsvHandle = dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
			commandList->OMSetRenderTargets(1, &rtvHandles[backBufferIndex], false, &dsvHandle);

			commandList->ClearRenderTargetView(rtvHandles[backBufferIndex], clearColor, 0, nullptr);
			commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

			ID3D12DescriptorHeap* descriptorHeaps[] = { srvDescriptorHeap };
			commandList->SetDescriptorHeaps(1, descriptorHeaps);

			commandList->SetPipelineState(graphicsPipelineState);
			commandList->SetGraphicsRootSignature(rootSignature);

			commandList->SetGraphicsRootConstantBufferView(0, materialResource->GetGPUVirtualAddress());

			// 1. 球体（Sphere）の描画設定
			commandList->SetGraphicsRootConstantBufferView(1, wvpResource->GetGPUVirtualAddress());

			commandList->SetGraphicsRootDescriptorTable(2, useMonsterBall ? srvHandleGPU2 : srvHandleGPU);

			commandList->SetGraphicsRootConstantBufferView(3, directionalLightResource->GetGPUVirtualAddress());

			commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
			commandList->DrawInstanced(kVertexCountSphere, 1, 0, 0);

			// 前の描画状態（モンスターボール）を引き継がないよう

			commandList->SetGraphicsRootDescriptorTable(2, srvHandleGPU); // Textureを再セット
			commandList->SetGraphicsRootConstantBufferView(3, directionalLightResource->GetGPUVirtualAddress()); // ライトを再セット

			commandList->IASetVertexBuffers(0, 1, &vertexBufferViewSprite);
			commandList->IASetIndexBuffer(&indexBufferViewSprite);
			commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);

#ifdef USE_IMGUI
			ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
#endif

			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
			commandList->ResourceBarrier(1, &barrier);

			hr = commandList->Close();
			assert(SUCCEEDED(hr));

			ID3D12CommandList* commandLists[] = { commandList };
			commandQueue->ExecuteCommandLists(1, commandLists);

			swapChain->Present(1, 0);

			fenceValue++;
			hr = commandQueue->Signal(fence, fenceValue);
			assert(SUCCEEDED(hr));

			if (fence->GetCompletedValue() < fenceValue) {
				hr = fence->SetEventOnCompletion(fenceValue, fenceEvent);
				assert(SUCCEEDED(hr));
				WaitForSingleObject(fenceEvent, INFINITE);
			}

			commandAllocator->Reset();
			commandList->Reset(commandAllocator, nullptr);
		}
	}

	// ========================
	// 解放
	// ========================
#ifdef USE_IMGUI
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
#endif

	graphicsPipelineState->Release();
	rootSignature->Release();
	vertexShaderBlob->Release();
	pixelShaderBlob->Release();
	signatureBlob->Release();
	if (errorBlob) errorBlob->Release();

	// 4. [リソース] バッファ・テクスチャ本体
	vertexResource->Release();
	vertexResourceSprite->Release();
	indexResourceSprite->Release();
	materialResource->Release();
	wvpResource->Release();
	transformationMatrixResourceSprite->Release();
	directionalLightResource->Release();
	textureResource->Release();
	textureResource2->Release();
	depthStencilResource->Release();

	// 5. [管理容器] デスクリプタヒープ
	rtvDescriptorHeap->Release();
	dsvDescriptorHeap->Release();
	srvDescriptorHeap->Release();
	samplerDescriptorHeap->Release();

	// 6. [システム管理] コマンド系・スワップチェーン
	commandList->Release();
	commandAllocator->Release();
	commandQueue->Release();
	swapChainResources[0]->Release();
	swapChainResources[1]->Release();
	swapChain->Release();
	fence->Release();
	CloseHandle(fenceEvent);

	// 7. [最下層] デバイスとファクトリ（親）
	device->Release();
	useAdapter->Release();
	dxgiFactory->Release();
#ifdef _DEBUG
	debugController->Release();
#endif

	IDXGIDebug1* debug;
	if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug)))) {
		debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
		debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
		debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);
		debug->Release();
	}

	return 0;
}