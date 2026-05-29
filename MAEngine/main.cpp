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

struct Material {
	Vector4 color;
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



// Windowsアプリでのエントリーポイント(main関数)
int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {

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

	// 失敗したら即終了
	assert(device != nullptr);

	logger.Log("Complete create D3D12Device!!");

	//SRV用のヒープでディスクリプタの数は128。SRVはShaderないで触れるものなので、ShaderVisibleはtrue
	ID3D12DescriptorHeap* srvDescriptorHeap =
		CreateDescriptorHeap(
			device,
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
			1,
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

	D3D12_ROOT_PARAMETER rootParameters[2]{};

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

	descriptionRootSignature.pParameters = rootParameters;    //ルートパラメータ配列へのポインタ
	descriptionRootSignature.NumParameters = _countof(rootParameters);    //配列の長さ



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
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[1]{};

	inputElementDescs[0].SemanticName = "POSITION";
	inputElementDescs[0].SemanticIndex = 0;
	inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	inputElementDescs[0].AlignedByteOffset =
		D3D12_APPEND_ALIGNED_ELEMENT;

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

	graphicsPipelineStateDesc.DepthStencilState.DepthEnable = false;
	graphicsPipelineStateDesc.DepthStencilState.StencilEnable = false;

	ID3D12PipelineState* graphicsPipelineState = nullptr;

	hr = device->CreateGraphicsPipelineState(
		&graphicsPipelineStateDesc,
		IID_PPV_ARGS(&graphicsPipelineState)
	);

	assert(SUCCEEDED(hr));

	// ========================
	// VertexResource
	// ========================

	// 頂点3つ分
	Vector4 vertices[3] = {
		{-0.5f, -0.5f, 0.0f, 1.0f}, // 左下
		{ 0.0f,  0.5f, 0.0f, 1.0f}, // 上
		{ 0.5f, -0.5f, 0.0f, 1.0f}, // 右下
	};

	ID3D12Resource* vertexResource =
		CreateBufferResource(
			device,
			sizeof(Vector4) * 3
		);

	// ========================
	// 頂点データを書き込む
	// ========================

	Vector4* vertexData = nullptr;

	vertexResource->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&vertexData)
	);

	memcpy(vertexData, vertices, sizeof(vertices));

	// ========================
	// VertexBufferView
	// ========================

	D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};

	vertexBufferView.BufferLocation =
		vertexResource->GetGPUVirtualAddress();

	vertexBufferView.SizeInBytes =
		sizeof(vertices);

	vertexBufferView.StrideInBytes =
		sizeof(Vector4);

	// ========================
	// MaterialResource
	// ========================

	// Material用Resource

	ID3D12Resource* materialResource =
		CreateBufferResource(
			device,
			(sizeof(Material) + 0xff) & ~0xff
		);

	//WVP用のリソースを作る。Matrix4x4 1つ分のサイズを用意する
	ID3D12Resource* wvpResource =
		CreateBufferResource(
			device,
			(sizeof(Matrix4x4) + 0xff) & ~0xff
		);

	//データを書き込む
	Matrix4x4* wvpData = nullptr;
	//書き込むためのアドレスを取得
	wvpResource->Map(0, nullptr, reinterpret_cast<void**>(&wvpData));
	//単位行列を書き込んでおく
	*wvpData = MakeIdentity4x4();

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
		0.0f,
		0.0f,
		1.0f
	};

	assert(SUCCEEDED(hr));

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

	// SwapChainのリソース
	// SwapChainが持つBackBufferリソース
	// ダブルバッファ用
	ID3D12Resource* swapChainResources[2] = {};

	hr = swapChain->GetBuffer(
		0,
		IID_PPV_ARGS(&swapChainResources[0])
	);
	assert(SUCCEEDED(hr));

	hr = swapChain->GetBuffer(
		1,
		IID_PPV_ARGS(&swapChainResources[1])
	);
	assert(SUCCEEDED(hr));

	// RTV作成
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2];

	rtvHandles[0] =
		rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

	UINT descriptorSize =
		device->GetDescriptorHandleIncrementSize(
			D3D12_DESCRIPTOR_HEAP_TYPE_RTV
		);

	rtvHandles[1].ptr =
		rtvHandles[0].ptr + descriptorSize;

	device->CreateRenderTargetView(
		swapChainResources[0],
		&rtvDesc,
		rtvHandles[0]
	);

	device->CreateRenderTargetView(
		swapChainResources[1],
		&rtvDesc,
		rtvHandles[1]
	);

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

	// ウィンドウを表示する
	ShowWindow(hwnd, SW_SHOW);

	MSG msg{};
	while (msg.message != WM_QUIT) {
		//Windowにメッセージが来てたら最優先で処理させる
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		} else {
			//ゲーム処理

#ifdef USE_IMGUI
			ImGui_ImplDX12_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();

			//開発用UIの処理、実際に開発用のUIを出す場合はここをゲーム固有の処理に書き換える
			ImGui::ShowDemoWindow();
#endif
			transform.rotate.y += 0.03f;
			
			Matrix4x4 worldMatrix = Matrix4x4::MakeAffineMatrix(
				transform.scale,
				transform.rotate,
				transform.translate
			);

			Matrix4x4 cameraMatrix = Matrix4x4::MakeAffineMatrix(cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate);

			Matrix4x4 viewMatrix = Matrix4x4::Inverse(cameraMatrix);

			Matrix4x4 projectionMatrix = Matrix4x4::MakePerspectiveFovMatrix(0.45f, float(kClientWidth) / float(kClientHeight), 0.01f, 100.0f);

			Matrix4x4 worldViewProjectionMatrix = Matrix4x4::Multiply(worldMatrix, Matrix4x4::Multiply(viewMatrix, projectionMatrix));

			*wvpData = worldViewProjectionMatrix;

#ifdef USE_IMGUI
			ImGui::Render();
#endif
			// 現在のBackBuffer番号
			UINT backBufferIndex =
				swapChain->GetCurrentBackBufferIndex();

			// GPUリソースは用途ごとに状態管理されるため
			// 描画前後で状態遷移が必要

			// BackBufferを画面表示用状態(Present)から
			// 描画可能状態(RenderTarget)へ変更
			D3D12_RESOURCE_BARRIER barrier{};

			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;

			barrier.Transition.pResource =
				swapChainResources[backBufferIndex];

			barrier.Transition.StateBefore =
				D3D12_RESOURCE_STATE_PRESENT;

			barrier.Transition.StateAfter =
				D3D12_RESOURCE_STATE_RENDER_TARGET;

			barrier.Transition.Subresource =
				D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

			commandList->ResourceBarrier(1, &barrier);

			// Viewport設定
			D3D12_VIEWPORT viewport{};

			viewport.Width = static_cast<float>(kClientWidth);
			viewport.Height = static_cast<float>(kClientHeight);
			viewport.TopLeftX = 0;
			viewport.TopLeftY = 0;
			viewport.MinDepth = 0.0f;
			viewport.MaxDepth = 1.0f;

			commandList->RSSetViewports(1, &viewport);

			// ScissorRect設定
			D3D12_RECT scissorRect{};

			scissorRect.left = 0;
			scissorRect.right = kClientWidth;
			scissorRect.top = 0;
			scissorRect.bottom = kClientHeight;

			commandList->RSSetScissorRects(1, &scissorRect);

			// 画面色
			float clearColor[] = {
				0.2f,
				0.6f,
				0.9f,
				1.0f
			};

			// RTV設定
			// 現在描画先として使うRTVを設定
			commandList->OMSetRenderTargets(
				1,
				&rtvHandles[backBufferIndex],
				false,
				nullptr
			);


			// 画面クリア
			commandList->ClearRenderTargetView(
				rtvHandles[backBufferIndex],
				clearColor,
				0,
				nullptr
			);
			
			ID3D12DescriptorHeap* descriptorHeaps[] = { srvDescriptorHeap };
			commandList->SetDescriptorHeaps(1, descriptorHeaps);

			// PSO設定
			commandList->SetGraphicsRootSignature(rootSignature);

			commandList->SetPipelineState(graphicsPipelineState);

			// MaterialのCBVをセット
			commandList->SetGraphicsRootConstantBufferView(
				0,
				materialResource->GetGPUVirtualAddress()
			);

			//wvp用のCBufferの場所を特定
			commandList->SetGraphicsRootConstantBufferView(
				1,
				wvpResource->GetGPUVirtualAddress()
			);

			// トポロジ設定
			commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			// VertexBuffer設定
			commandList->IASetVertexBuffers(0, 1, &vertexBufferView);

			// 描画
			commandList->DrawInstanced(3, 1, 0, 0);

#ifdef USE_IMGUI
			ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
#endif

			// 描画完了後、RenderTarget状態から
			// Present状態へ戻す
			barrier.Transition.StateBefore =
				D3D12_RESOURCE_STATE_RENDER_TARGET;

			barrier.Transition.StateAfter =
				D3D12_RESOURCE_STATE_PRESENT;

			commandList->ResourceBarrier(1, &barrier);

			// コマンド閉じる
			hr = commandList->Close();
			assert(SUCCEEDED(hr));

			// GPUへ送る
			ID3D12CommandList* commandLists[] = {
				commandList
			};

			// 記録済みコマンドをGPUへ送信
			commandQueue->ExecuteCommandLists(
				1,
				commandLists
			);


			// BackBufferとFrontBufferを交換して画面表示
			// 同時に次の描画用Bufferへ切り替わる
			swapChain->Present(1, 0);

			// Fence値を更新
			fenceValue++;

			// GPUにSignalを送る
			hr = commandQueue->Signal(
				fence,
				fenceValue
			);

			assert(SUCCEEDED(hr));

			// GPUが終わるまで待つ
			if (fence->GetCompletedValue() < fenceValue) {

				hr = fence->SetEventOnCompletion(
					fenceValue,
					fenceEvent
				);

				assert(SUCCEEDED(hr));

				WaitForSingleObject(
					fenceEvent,
					INFINITE
				);
			}

			// 次フレーム用

			// GPU実行完了後なので再利用可能
			// コマンド記録領域をリセット
			commandAllocator->Reset();

			// コマンドリストを初期状態に戻す
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


	// VertexResource
	vertexResource->Release();

	materialResource->Release();

	// PSO
	graphicsPipelineState->Release();

	// Shader
	vertexShaderBlob->Release();
	pixelShaderBlob->Release();

	// RootSignature
	rootSignature->Release();

	// Blob
	signatureBlob->Release();

	if (errorBlob) {
		errorBlob->Release();
	}

	// DXC
	includeHandler->Release();
	dxcCompiler->Release();
	dxcUtils->Release();

	// SwapChainResources
	swapChainResources[0]->Release();
	swapChainResources[1]->Release();

	// RTVHeap
	rtvDescriptorHeap->Release();

	// SwapChain
	swapChain->Release();

	// Fence
	CloseHandle(fenceEvent);
	fence->Release();

	// Command系
	commandList->Release();
	commandAllocator->Release();
	commandQueue->Release();

	// Device
	device->Release();

	// Adapter
	useAdapter->Release();

	// Factory
	dxgiFactory->Release();

	wvpResource->Release();


#ifdef _DEBUG
	debugController->Release();
#endif
	srvDescriptorHeap->Release();

	CloseWindow(hwnd);

	//リソースリークチェック
	// DXGIデバッグインターフェース取得
	// 終了時に未解放リソースを出力する
	IDXGIDebug1* debug;
	if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug)))) {
		debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
		debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
		debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);
		debug->Release();
	}


	return 0;
}