#include <Windows.h>
#include <cstdint>
#include<string>
#include <format>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cassert>
#include <DbgHelp.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "Dbghelp.lib")

namespace fs = std::filesystem;

// ログファイルパスを保持
fs::path g_logFilePath;

void Log(const std::string& message) {
	// デバッグ出力
	OutputDebugStringA((message + "\n").c_str());

	// ファイル書き込み
	std::ofstream ofs(g_logFilePath, std::ios::app);
	if (ofs) {
		ofs << message << std::endl;
	}
}

std::string GetTimeStringForFile() {
	auto now = std::chrono::system_clock::now();
	std::time_t t = std::chrono::system_clock::to_time_t(now);

	std::tm tm{};
	localtime_s(&tm, &t);

	std::ostringstream oss;
	oss << std::put_time(&tm, "%Y%m%d_%H%M%S");
	return oss.str();
}

void InitLog() {
	// logsフォルダ作成
	fs::create_directories("logs");

	// logs/20260424_153000.log みたいな名前
	std::string filename = GetTimeStringForFile() + ".log";
	g_logFilePath = fs::path("logs") / filename;
}

// ウィンドウプロシージャー
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
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

	Log("Crash dump generated.");

	return EXCEPTION_EXECUTE_HANDLER;
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

	InitLog();

	Log("ログ開始");

	// 出力ウィンドへの文字出力
	OutputDebugStringA("Hello,DirectX!\n");

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

	for (UINT i = 0;
		dxgiFactory->EnumAdapterByGpuPreference(
			i,
			DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
			IID_PPV_ARGS(&useAdapter)) != DXGI_ERROR_NOT_FOUND;
		++i) {

		DXGI_ADAPTER_DESC3 adapterDesc{};
		hr = useAdapter->GetDesc3(&adapterDesc);
		assert(SUCCEEDED(hr));

		// ソフトウェアGPUは除外
		if (!(adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)) {
			OutputDebugStringW(L"Use Adapter: ");
			OutputDebugStringW(adapterDesc.Description);
			OutputDebugStringW(L"\n");
			break;
		}

		useAdapter = nullptr;
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
			Log(std::format("FeatureLevel : {}\n", featureLevelStrings[i]));
			break;
		}
	}

	// 失敗したら即終了
	assert(device != nullptr);

	Log("Complete create D3D12Device!!\n");

	// ウィンドウを表示する
	ShowWindow(hwnd, SW_SHOW);

	MSG msg{};
	while (msg.message != WM_QUIT) {
		//Windowにメッセージが来てたら最優先で処理させる
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else {
			//ゲーム処理
		}
	}


	return 0;
}