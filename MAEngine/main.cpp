#include <Windows.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
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
#include <wrl.h>
#include<xaudio2.h>
#include <vector>
#include"Matrix4x4.h"
#include"Vector3.h"

#ifdef USE_IMGUI
#include"externals/imgui/imgui.h"
#include"externals/imgui/imgui_impl_dx12.h"
#include"externals/imgui/imgui_impl_win32.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif
#include"externals/DirectXTex/DirectXTex.h"
#include "externals/DirectXTex/d3dx12.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "Dbghelp.lib")
#pragma comment(lib,"dxguid.lib")
#pragma comment(lib,"dxcompiler.lib")
#pragma comment(lib,"xaudio2.lib")
#pragma comment(lib, "dinput8.lib")

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
	// shaderSource を ComPtr で宣言
	Microsoft::WRL::ComPtr<IDxcBlobEncoding> shaderSource;
	HRESULT hr = dxcUtils->LoadFile(
		filePath.c_str(),
		nullptr,
		shaderSource.GetAddressOf() // ComPtr は GetAddressOf() で受ける
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

	// shaderResult も ComPtr で宣言
	Microsoft::WRL::ComPtr<IDxcResult> shaderResult;
	hr = dxcCompiler->Compile(
		&shaderSourceBuffer,
		arguments,
		_countof(arguments),
		includeHandler,
		IID_PPV_ARGS(&shaderResult) // .GetAddressOf() と同じ意味
	);
	assert(SUCCEEDED(hr));

	// Error確認
	Microsoft::WRL::ComPtr<IDxcBlobUtf8> shaderError;
	shaderResult->GetOutput(
		DXC_OUT_ERRORS,
		IID_PPV_ARGS(&shaderError),
		nullptr
	);

	if (shaderError != nullptr && shaderError->GetStringLength() != 0) {
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

// 描画切り替えフラグ
bool enablePlaneDraw = true;   // 平面
bool enableModelDraw = true;  // モデル
bool enableSphereDraw = false; // 球体
bool enableSpriteDraw = false; // スプライト

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
	int32_t lightingType;
	float padding[2];
	Matrix4x4 uvTransform;
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

// 頂点構造体の定義
struct VertexData {
	Vector4 position;
	Vector2 texcoord;
	Vector3 normal;
};

struct MaterialData {
	std::string textureFilePath;
};

// 1つのメッシュが持つデータ
struct MeshData {
	std::vector<VertexData> vertices;
	MaterialData material;
};

// モデル全体（複数のメッシュやマテリアルを保持）
struct ModelData {
	std::vector<MeshData> meshes;
};

struct MeshResource {
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexBufferResource;
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView;
	// 必要に応じてマテリアル用のリソースやSRVインデックスもここへ持たせる
};

struct ModelResource {
	std::vector<MeshResource> meshes;
};

// 音声データ読み込み用構造体
struct ChunkHeader {
	char id[4];
	int32_t size;
};

struct RiffHeader {
	ChunkHeader chunk;
	char type[4];
};

struct FormatChunk {
	ChunkHeader chunk;
	WAVEFORMATEX fmt;
};

struct SoundData {
	WAVEFORMATEX wfex;
	BYTE* pBuffer;
	unsigned int bufferSize;
};

void SoundUnload(SoundData* soundData)
{
	// 確保したバッファのメモリを解放
	delete[] soundData->pBuffer;

	// ポインタを無効化し、サイズを0に戻して安全にする
	soundData->pBuffer = nullptr;
	soundData->bufferSize = 0;
	soundData->wfex = {};
}

// 音声再生
void SoundPlayWave(IXAudio2* xAudio2, const SoundData& soundData) {
	HRESULT result;

	// 波形フォーマットを元にSourceVoiceの生成
	IXAudio2SourceVoice* pSourceVoice = nullptr;
	result = xAudio2->CreateSourceVoice(&pSourceVoice, &soundData.wfex);
	assert(SUCCEEDED(result));

	// 再生する波形データの設定
	XAUDIO2_BUFFER buf{};
	buf.pAudioData = soundData.pBuffer;
	buf.AudioBytes = soundData.bufferSize;
	buf.Flags = XAUDIO2_END_OF_STREAM;

	// 波形データの再生
	result = pSourceVoice->SubmitSourceBuffer(&buf);
	assert(SUCCEEDED(result));

	result = pSourceVoice->Start();
	assert(SUCCEEDED(result));
}

MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename) {
	MaterialData materialData; // 構築するデータ
	std::string line;
	std::ifstream file(directoryPath + "/" + filename);
	assert(file.is_open()); // ファイルが開けなかったら止める

	while (std::getline(file, line)) {
		std::string identifier;
		std::stringstream lineStream(line);
		lineStream >> identifier;

		// コメントや空行は飛ばす
		if (identifier == "map_Kd") {
			std::string textureFilename;
			lineStream >> textureFilename;
			// 連結してフルパスにする
			materialData.textureFilePath = directoryPath + "/" + textureFilename;
		}
	}
	return materialData;
}

ModelData LoadObjFile(const std::string& directoryPath, const std::string& filename) {
	ModelData modelData;
	std::vector<Vector4> positions;
	std::vector<Vector3> normals;
	std::vector<Vector2> texcoords;
	std::string line;

	std::ifstream file(directoryPath + "/" + filename);
	assert(file.is_open());

	// 現在構築中のメッシュ（最初は空のメッシュを一つ追加しておく）
	modelData.meshes.emplace_back();
	MeshData* currentMesh = &modelData.meshes.back();
	std::string currentMaterialFilename;

	while (std::getline(file, line)) {
		std::string identifier;
		std::stringstream lineStream(line);
		lineStream >> identifier;

		if (identifier == "v") {
			Vector4 position;
			lineStream >> position.x >> position.y >> position.z;
			position.w = 1.0f;
			position.x *= -1.0f; // X軸反転
			positions.push_back(position);
		} else if (identifier == "vt") {
			Vector2 texcoord;
			lineStream >> texcoord.u >> texcoord.v;
			texcoord.v = 1.0f - texcoord.v; // V成分反転
			texcoords.push_back(texcoord);
		} else if (identifier == "vn") {
			Vector3 normal;
			lineStream >> normal.x >> normal.y >> normal.z;
			normal.x *= -1.0f; // X軸反転
			normals.push_back(normal);
		} else if (identifier == "mtllib") {
			std::string materialFilename;
			lineStream >> materialFilename;
			currentMaterialFilename = materialFilename;
		} else if (identifier == "usemtl") {
			std::string materialName;
			lineStream >> materialName;

			// すでに頂点が入っているメッシュであれば、新しいメッシュを切り替えて追加する
			if (!currentMesh->vertices.empty()) {
				modelData.meshes.emplace_back();
				currentMesh = &modelData.meshes.back();
				// マテリアルを引き継ぐか、新しく読み込む
				currentMesh->material = LoadMaterialTemplateFile(directoryPath, currentMaterialFilename);
			} else {
				// 最初のエントリならマテリアルをロードして設定
				currentMesh->material = LoadMaterialTemplateFile(directoryPath, currentMaterialFilename);
			}
		} else if (identifier == "f") {
			VertexData triangle[3];
			for (int32_t i = 0; i < 3; ++i) {
				std::string vertexDefinition;
				lineStream >> vertexDefinition;

				std::stringstream vertexStream(vertexDefinition);
				std::string indexString;
				std::vector<int32_t> indices;
				while (std::getline(vertexStream, indexString, '/')) {
					indices.push_back(indexString.empty() ? 0 : std::stoi(indexString));
				}

				int32_t positionIndex = indices[0] - 1;
				int32_t texcoordIndex = indices[1] - 1;
				int32_t normalIndex = indices[2] - 1;

				triangle[i].position = positions[positionIndex];
				triangle[i].texcoord = texcoords[texcoordIndex];
				triangle[i].normal = normals[normalIndex];
			}
			// 頂点順序を逆にして追加
			currentMesh->vertices.push_back(triangle[2]);
			currentMesh->vertices.push_back(triangle[1]);
			currentMesh->vertices.push_back(triangle[0]);
		}
	}
	return modelData;
}

SoundData SoundLoadWave(const char* filename) {
	// ファイルのオープン
	std::ifstream file;
	file.open(filename, std::ios_base::binary);
	assert(file.is_open());

	// RIFFヘッダーの読み込みとチェック
	RiffHeader riff;
	file.read((char*)&riff, sizeof(riff));
	assert(strncmp(riff.chunk.id, "RIFF", 4) == 0);
	assert(strncmp(riff.type, "WAVE", 4) == 0);

	// Formatチャンクの読み込み
	FormatChunk format = {};
	file.read((char*)&format, sizeof(ChunkHeader)); // 先にヘッダーのみ読み込む
	if (strncmp(format.chunk.id, "fmt ", 4) != 0) {
		assert(0);
	}
	assert(format.chunk.size <= sizeof(format.fmt));
	file.read((char*)&format.fmt, format.chunk.size); // 本体を読み込む

	// Dataチャンクの読み込み（JUNKチャンクのスキップ対応）
	ChunkHeader data;
	file.read((char*)&data, sizeof(data));

	// JUNKチャンクがあれば読み飛ばす
	if (strncmp(data.id, "JUNK", 4) == 0) {
		file.seekg(data.size, std::ios_base::cur);
		file.read((char*)&data, sizeof(data)); // 本来のdataチャンクを読み直す
	}

	// データチャンクの確認
	assert(strncmp(data.id, "data", 4) == 0);

	// 波形データの読み込み
	char* pBuffer = new char[data.size];
	file.read(pBuffer, data.size);

	// ファイルを閉じる
	file.close();

	// 読み込んだデータを構造体に格納して返す
	SoundData soundData = {};
	soundData.wfex = format.fmt;
	soundData.pBuffer = reinterpret_cast<BYTE*>(pBuffer);
	soundData.bufferSize = data.size;

	return soundData;
}

DirectX::ScratchImage LoadTexture(const std::string& filePath) {
	DirectX::ScratchImage mipImages{};

	std::wstring texturePathW(filePath.begin(), filePath.end());

	// もともと使っていた読み込み処理を実行
	HRESULT hr = DirectX::LoadFromWICFile(
		texturePathW.c_str(),
		DirectX::WIC_FLAGS_NONE,
		nullptr,
		mipImages
	);
	assert(SUCCEEDED(hr));

	// 読み込んだ画像をそのまま返す
	return mipImages;
}

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

// === VRAM上にテクスチャリソースを作成する関数 ===
ID3D12Resource* CreateTextureResource(ID3D12Device* device, const DirectX::TexMetadata& metadata) {
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Width = static_cast<UINT64>(metadata.width);
	resourceDesc.Height = static_cast<UINT>(metadata.height);
	resourceDesc.MipLevels = static_cast<UINT16>(metadata.mipLevels);
	resourceDesc.DepthOrArraySize = static_cast<UINT16>(metadata.arraySize);
	resourceDesc.Format = metadata.format;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Dimension = static_cast<D3D12_RESOURCE_DIMENSION>(metadata.dimension);

	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT; // VRAM上に作成

	ID3D12Resource* resource = nullptr;
	HRESULT hr = device->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_COPY_DEST, // データ転送を受け取れる状態にする
		nullptr,
		IID_PPV_ARGS(&resource)
	);
	assert(SUCCEEDED(hr));

	return resource;
}

// === テクスチャデータを中間リソース経由でVRAMへ転送する関数 ===
[[nodiscard]]
ID3D12Resource* UploadTextureData(
	ID3D12Resource* texture,
	const DirectX::ScratchImage& mipImages,
	ID3D12Device* device,
	ID3D12GraphicsCommandList* commandList)
{
	std::vector<D3D12_SUBRESOURCE_DATA> subresources;
	DirectX::PrepareUpload(device, mipImages.GetImages(), mipImages.GetImageCount(), mipImages.GetMetadata(), subresources);

	uint64_t intermediateSize = GetRequiredIntermediateSize(texture, 0, UINT(subresources.size()));
	ID3D12Resource* intermediateResource = CreateBufferResource(device, intermediateSize);

	UpdateSubresources(commandList, texture, intermediateResource, 0, 0, UINT(subresources.size()), subresources.data());

	// 転送完了後、シェーダー参照用にStateを変更 (COPY_DEST -> GENERIC_READ)
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = texture;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;

	commandList->ResourceBarrier(1, &barrier);

	return intermediateResource;
}

ID3D12Resource* CreateDepthStencilTextureResource(
	ID3D12Device* d3dDevice,
	int32_t width,
	int32_t height)
{
	// 生成するResourceの設定
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Width = static_cast<UINT64>(width);       // Textureの幅
	resourceDesc.Height = static_cast<UINT>(height);       // Textureの高さ
	resourceDesc.MipLevels = 1;                            // mipmapの数
	resourceDesc.DepthOrArraySize = 1;                     // 奥行き or 配列Textureの配列数
	resourceDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;   // DepthStencilとして利用可能なフォーマット
	resourceDesc.SampleDesc.Count = 1;                     // サンプリングカウント。1固定。
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; // 2次元
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL; // DepthStencilとして使う通知

	// 利用するHeapの設定
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;         // VRAM上に作る

	// 深度値のクリア設定
	D3D12_CLEAR_VALUE depthClearValue{};
	depthClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depthClearValue.DepthStencil.Depth = 1.0f;
	depthClearValue.DepthStencil.Stencil = 0;

	// Resourceの生成
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

//リークチェック構造体
struct D3DResourceLeakChecker {
	~D3DResourceLeakChecker() {
		Microsoft::WRL::ComPtr<IDXGIDebug1> debug;
		if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug)))) {
			debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
			debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
			debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);
		}
	}
};

// 1. キーを押している状態か
bool IsPressKey(BYTE* key, uint8_t keyNumber) {
	return (key[keyNumber] & 0x80) != 0;
}

// 2. キーを離している状態か
bool IsReleaseKey(BYTE* key, uint8_t keyNumber) {
	return (key[keyNumber] & 0x80) == 0;
}

// 3. キーを押した瞬間か
bool IsTriggerKey(BYTE* key, BYTE* keyPre, uint8_t keyNumber) {
	return ((key[keyNumber] & 0x80) != 0) && ((keyPre[keyNumber] & 0x80) == 0);
}

// 4. キーを離した瞬間か
bool IsReleaseTriggerKey(BYTE* key, BYTE* keyPre, uint8_t keyNumber) {
	return ((key[keyNumber] & 0x80) == 0) && ((keyPre[keyNumber] & 0x80) != 0);
}

// Windowsアプリでのエントリーポイント(main関数)
int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {
	CoInitializeEx(0, COINIT_MULTITHREADED);

	// まずリークチェッカーを宣言する
	D3DResourceLeakChecker leakCheck;

	Microsoft::WRL::ComPtr<ID3D12Device> device;

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
	Microsoft::WRL::ComPtr<IDXGIFactory7> dxgiFactory;
	HRESULT hr = CreateDXGIFactory(IID_PPV_ARGS(&dxgiFactory));
	assert(SUCCEEDED(hr));

	// ========================
	// アダプタ選択
	// ========================
	Microsoft::WRL::ComPtr<IDXGIAdapter4> useAdapter;

	for (UINT i = 0;; ++i) {

		Microsoft::WRL::ComPtr<IDXGIAdapter4> adapter;

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
			useAdapter.Get(),
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

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap;
	srvDescriptorHeap.Attach(
		CreateDescriptorHeap(
			device.Get(),
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
			128,
			true
		)
	);

#ifdef _DEBUG

	Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue;
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

	}
#endif // _DEBUG

	// コマンドキューを生成する
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue;
	D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};

	hr = device->CreateCommandQueue(
		&commandQueueDesc,
		IID_PPV_ARGS(&commandQueue)
	);

	assert(SUCCEEDED(hr));

	// XAudio2の初期化
	Microsoft::WRL::ComPtr<IXAudio2> xAudio2;
	IXAudio2MasteringVoice* masterVoice = nullptr;

	hr = XAudio2Create(&xAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR);
	assert(SUCCEEDED(hr));

	hr = xAudio2->CreateMasteringVoice(&masterVoice);
	assert(SUCCEEDED(hr));

	// コマンドアロケータを生成する
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator;

	hr = device->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(&commandAllocator)
	);

	assert(SUCCEEDED(hr));

	// コマンドリストを生成する
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList;

	hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList));
	assert(SUCCEEDED(hr));

	// ========================
	// Fence生成
	// ========================

	// GPU同期用Fence
	Microsoft::WRL::ComPtr<ID3D12Fence> fence;

	// Fenceの現在値
	uint64_t fenceValue = 0;

	// Fenceオブジェクト生成
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

	Microsoft::WRL::ComPtr<IDxcUtils> dxcUtils;
	hr = DxcCreateInstance(
		CLSID_DxcUtils,
		IID_PPV_ARGS(&dxcUtils)
	);
	assert(SUCCEEDED(hr));

	Microsoft::WRL::ComPtr<IDxcCompiler3> dxcCompiler;
	hr = DxcCreateInstance(
		CLSID_DxcCompiler,
		IID_PPV_ARGS(&dxcCompiler)
	);
	assert(SUCCEEDED(hr));

	Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler;
	hr = dxcUtils->CreateDefaultIncludeHandler(
		&includeHandler
	);
	assert(SUCCEEDED(hr));

	// ========================
	// RootSignature
	// ========================

	D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
	descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	D3D12_ROOT_PARAMETER rootParameters[4]{};
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[0].Descriptor.ShaderRegister = 0;
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[1].Descriptor.ShaderRegister = 0;
	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

	D3D12_DESCRIPTOR_RANGE descriptorRange[1]{};
	descriptorRange[0].BaseShaderRegister = 0;
	descriptorRange[0].NumDescriptors = 1;
	descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRange;
	rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange);

	descriptionRootSignature.pParameters = rootParameters;
	descriptionRootSignature.NumParameters = _countof(rootParameters);

	rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[3].Descriptor.ShaderRegister = 1;

	D3D12_STATIC_SAMPLER_DESC staticSamplers[1]{};
	staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
	staticSamplers[0].ShaderRegister = 0;
	staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	descriptionRootSignature.pStaticSamplers = staticSamplers;
	descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers);

	Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;

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

	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;

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
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[3]{};
	inputElementDescs[0].SemanticName = "POSITION";
	inputElementDescs[0].SemanticIndex = 0;
	inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	inputElementDescs[1].SemanticName = "TEXCOORD";
	inputElementDescs[1].SemanticIndex = 0;
	inputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT;
	inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

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
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	blendDesc.RenderTarget[0].BlendEnable = TRUE;
	blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;

	// ========================
	// RasterizerState
	// ========================
	D3D12_RASTERIZER_DESC rasterizerDesc{};
	rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

	// ========================
	// Shader Compile
	// ========================
	Microsoft::WRL::ComPtr<IDxcBlob> vertexShaderBlob;
	vertexShaderBlob.Attach(CompileShader(
		L"Object3d.VS.hlsl",
		L"vs_6_0",
		dxcUtils.Get(),
		dxcCompiler.Get(),
		includeHandler.Get()
	));

	Microsoft::WRL::ComPtr<IDxcBlob> pixelShaderBlob;
	pixelShaderBlob.Attach(CompileShader(
		L"Object3d.PS.hlsl",
		L"ps_6_0",
		dxcUtils.Get(),
		dxcCompiler.Get(),
		includeHandler.Get()
	));

	// ========================
	// PSO
	// ========================
	D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc{};
	graphicsPipelineStateDesc.pRootSignature = rootSignature.Get();
	graphicsPipelineStateDesc.InputLayout = inputLayoutDesc;
	graphicsPipelineStateDesc.VS.pShaderBytecode = vertexShaderBlob->GetBufferPointer();
	graphicsPipelineStateDesc.VS.BytecodeLength = vertexShaderBlob->GetBufferSize();
	graphicsPipelineStateDesc.PS.pShaderBytecode = pixelShaderBlob->GetBufferPointer();
	graphicsPipelineStateDesc.PS.BytecodeLength = pixelShaderBlob->GetBufferSize();
	graphicsPipelineStateDesc.BlendState = blendDesc;
	graphicsPipelineStateDesc.RasterizerState = rasterizerDesc;
	graphicsPipelineStateDesc.NumRenderTargets = 1;
	graphicsPipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	graphicsPipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	graphicsPipelineStateDesc.SampleDesc.Count = 1;
	graphicsPipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	graphicsPipelineStateDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
	graphicsPipelineStateDesc.DepthStencilState.DepthEnable = TRUE;
	graphicsPipelineStateDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	graphicsPipelineStateDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	graphicsPipelineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

	Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState;
	hr = device->CreateGraphicsPipelineState(
		&graphicsPipelineStateDesc,
		IID_PPV_ARGS(&graphicsPipelineState)
	);
	assert(SUCCEEDED(hr));


	// =========================================================
	// 通常アルファブレンディング（非加算合成）用 PSO の作成
	// =========================================================
	D3D12_BLEND_DESC blendDescAlpha{};
	blendDescAlpha.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	blendDescAlpha.RenderTarget[0].BlendEnable = TRUE;
	blendDescAlpha.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	blendDescAlpha.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA; // 通常アルファ合成
	blendDescAlpha.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	blendDescAlpha.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	blendDescAlpha.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
	blendDescAlpha.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDescFence = graphicsPipelineStateDesc;
	graphicsPipelineStateDescFence.BlendState = blendDescAlpha;

	graphicsPipelineStateDescFence.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

	Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineStateFence;
	hr = device->CreateGraphicsPipelineState(
		&graphicsPipelineStateDescFence,
		IID_PPV_ARGS(&graphicsPipelineStateFence)
	);
	assert(SUCCEEDED(hr));

	// ================================================
// Particle用 RootSignature の作成
// ================================================

// 1. ピクセルシェーダー用テクスチャの DescriptorRange (t0)
	D3D12_DESCRIPTOR_RANGE descriptorRangeParticle[1]{};
	descriptorRangeParticle[0].BaseShaderRegister = 0;
	descriptorRangeParticle[0].NumDescriptors = 1;
	descriptorRangeParticle[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descriptorRangeParticle[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	// 2. 頂点シェーダー用 Instancing (StructuredBuffer) の DescriptorRange (t0)
	D3D12_DESCRIPTOR_RANGE descriptorRangeForInstancing[1]{};
	descriptorRangeForInstancing[0].BaseShaderRegister = 0; // t0
	descriptorRangeForInstancing[0].NumDescriptors = 1;
	descriptorRangeForInstancing[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; // SRVを使う
	descriptorRangeForInstancing[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	// ルートパラメータの設定 (※PixelShader側でLightを使わない場合は全3個に減らします)
	D3D12_ROOT_PARAMETER particleRootParameters[3]{};

	// [0] Material用 CBV (PixelShader)
	particleRootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	particleRootParameters[0].Descriptor.ShaderRegister = 0;
	particleRootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	// [1] Instancing用 StructuredBuffer (VertexShader, SRV: t0)
	particleRootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	particleRootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	particleRootParameters[1].DescriptorTable.pDescriptorRanges = descriptorRangeForInstancing;
	particleRootParameters[1].DescriptorTable.NumDescriptorRanges = _countof(descriptorRangeForInstancing);

	// [2] Texture用 SRV (PixelShader, SRV: t0)
	particleRootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	particleRootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	particleRootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRangeParticle;
	particleRootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(descriptorRangeParticle);

	// 静的サンプラー設定
	D3D12_STATIC_SAMPLER_DESC particleStaticSamplers[1]{};
	particleStaticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	particleStaticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	particleStaticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	particleStaticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	particleStaticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	particleStaticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
	particleStaticSamplers[0].ShaderRegister = 0;
	particleStaticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	// Description設定
	D3D12_ROOT_SIGNATURE_DESC particleDescriptionRootSignature{};
	particleDescriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	particleDescriptionRootSignature.pParameters = particleRootParameters;
	particleDescriptionRootSignature.NumParameters = _countof(particleRootParameters);
	particleDescriptionRootSignature.pStaticSamplers = particleStaticSamplers;
	particleDescriptionRootSignature.NumStaticSamplers = _countof(particleStaticSamplers);

	// シリアライズと生成
	Microsoft::WRL::ComPtr<ID3DBlob> particleSignatureBlob;
	Microsoft::WRL::ComPtr<ID3DBlob> particleErrorBlob;

	hr = D3D12SerializeRootSignature(
		&particleDescriptionRootSignature,
		D3D_ROOT_SIGNATURE_VERSION_1,
		&particleSignatureBlob,
		&particleErrorBlob
	);

	if (FAILED(hr)) {
		logger.Log((char*)particleErrorBlob->GetBufferPointer());
		assert(false);
	}

	Microsoft::WRL::ComPtr<ID3D12RootSignature> particleRootSignature;

	hr = device->CreateRootSignature(
		0,
		particleSignatureBlob->GetBufferPointer(),
		particleSignatureBlob->GetBufferSize(),
		IID_PPV_ARGS(&particleRootSignature)
	);
	assert(SUCCEEDED(hr));


	// ================================================
	// Particle用 各種ステート生成 (設定変更を見越して個別作成) [スライド指示]
	// ================================================

	// InputLayout
	D3D12_INPUT_ELEMENT_DESC particleInputElementDescs[3]{};
	particleInputElementDescs[0].SemanticName = "POSITION";
	particleInputElementDescs[0].SemanticIndex = 0;
	particleInputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	particleInputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	particleInputElementDescs[1].SemanticName = "TEXCOORD";
	particleInputElementDescs[1].SemanticIndex = 0;
	particleInputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT;
	particleInputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	particleInputElementDescs[2].SemanticName = "NORMAL";
	particleInputElementDescs[2].SemanticIndex = 0;
	particleInputElementDescs[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	particleInputElementDescs[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	D3D12_INPUT_LAYOUT_DESC particleInputLayoutDesc{};
	particleInputLayoutDesc.pInputElementDescs = particleInputElementDescs;
	particleInputLayoutDesc.NumElements = _countof(particleInputElementDescs);

	// BlendState (アルファブレンドまたは加算合成)
	D3D12_BLEND_DESC particleBlendDesc{};
	particleBlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	particleBlendDesc.RenderTarget[0].BlendEnable = TRUE;
	particleBlendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	particleBlendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	particleBlendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	particleBlendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	particleBlendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	particleBlendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;

	// RasterizerState
	D3D12_RASTERIZER_DESC particleRasterizerDesc{};
	particleRasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;
	particleRasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

	// DepthStencilDesc (半透明描画に対応するためDepthWriteMaskをZEROにするのが一般的です)
	D3D12_DEPTH_STENCIL_DESC particleDepthStencilDesc{};
	particleDepthStencilDesc.DepthEnable = TRUE;
	particleDepthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; // 深度の書き込みをOFFに設定
	particleDepthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;


	// ================================================
	// Particle用 Shader Compile [スライド指示]
	// ================================================
	Microsoft::WRL::ComPtr<IDxcBlob> particleVSBlob;
	particleVSBlob.Attach(CompileShader(
		L"Particle.VS.hlsl",
		L"vs_6_0",
		dxcUtils.Get(),
		dxcCompiler.Get(),
		includeHandler.Get()
	));

	Microsoft::WRL::ComPtr<IDxcBlob> particlePSBlob;
	particlePSBlob.Attach(CompileShader(
		L"Particle.PS.hlsl",
		L"ps_6_0",
		dxcUtils.Get(),
		dxcCompiler.Get(),
		includeHandler.Get()
	));


	// ================================================
	// Particle用 GraphicsPipelineState の生成
	// ================================================
	D3D12_GRAPHICS_PIPELINE_STATE_DESC particlePipelineStateDesc{};
	particlePipelineStateDesc.pRootSignature = particleRootSignature.Get(); // Particle用RootSignatureを設定
	particlePipelineStateDesc.InputLayout = particleInputLayoutDesc;
	particlePipelineStateDesc.VS.pShaderBytecode = particleVSBlob->GetBufferPointer(); // Particle用VSを設定
	particlePipelineStateDesc.VS.BytecodeLength = particleVSBlob->GetBufferSize();
	particlePipelineStateDesc.PS.pShaderBytecode = particlePSBlob->GetBufferPointer(); // Particle用PSを設定
	particlePipelineStateDesc.PS.BytecodeLength = particlePSBlob->GetBufferSize();
	particlePipelineStateDesc.BlendState = particleBlendDesc;
	particlePipelineStateDesc.RasterizerState = particleRasterizerDesc;
	particlePipelineStateDesc.DepthStencilState = particleDepthStencilDesc;
	particlePipelineStateDesc.NumRenderTargets = 1;
	particlePipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	particlePipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	particlePipelineStateDesc.SampleDesc.Count = 1;
	particlePipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	particlePipelineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

	Microsoft::WRL::ComPtr<ID3D12PipelineState> particlePipelineState;
	hr = device->CreateGraphicsPipelineState(
		&particlePipelineStateDesc,
		IID_PPV_ARGS(&particlePipelineState)
	);
	assert(SUCCEEDED(hr));

	const uint32_t kSubdivision = 16;
	const uint32_t kVertexCountSphere = kSubdivision * kSubdivision * 6;
	std::vector<VertexData> sphereVertices(kVertexCountSphere);

	// 球の頂点データ
	{
		const float kPi = 3.1415926535f;
		float latStep = kPi / kSubdivision;
		float lonStep = 2.0f * kPi / kSubdivision;
		uint32_t vertexIndex = 0;

		for (uint32_t lat = 0; lat < kSubdivision; ++lat) {
			float theta0 = lat * latStep;
			float theta1 = (lat + 1) * latStep;

			for (uint32_t lon = 0; lon < kSubdivision; ++lon) {
				float phi0 = lon * lonStep;
				float phi1 = (lon + 1) * lonStep;

				auto GetSphereVertex = [](float theta, float phi) -> VertexData {
					VertexData v{};
					v.position.x = std::sin(theta) * std::cos(phi);
					v.position.y = std::cos(theta);
					v.position.z = std::sin(theta) * std::sin(phi);
					v.position.w = 1.0f;
					v.normal.x = v.position.x;
					v.normal.y = v.position.y;
					v.normal.z = v.position.z;
					v.texcoord.u = phi / (2.0f * 3.1415926535f);
					v.texcoord.v = theta / 3.1415926535f;
					return v;
					};

				VertexData v00 = GetSphereVertex(theta0, phi0);
				VertexData v10 = GetSphereVertex(theta1, phi0);
				VertexData v01 = GetSphereVertex(theta0, phi1);
				VertexData v11 = GetSphereVertex(theta1, phi1);

				// 三角形1
				sphereVertices[vertexIndex++] = v00;
				sphereVertices[vertexIndex++] = v10;
				sphereVertices[vertexIndex++] = v01;
				// 三角形2
				sphereVertices[vertexIndex++] = v01;
				sphereVertices[vertexIndex++] = v10;
				sphereVertices[vertexIndex++] = v11;
			}
		}
	}

	OutputDebugStringA(("C++ Size: " + std::to_string(sizeof(TransformationMatrix))).c_str());

	// --- 左手座標系（時計回り）の平面頂点データ ---
	std::vector<VertexData> planeVertices = {
		// 1つ目の三角形 (左上 -> 右上 -> 左下)
		{ { -0.5f,  0.5f, 0.0f, 1.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f } }, // 左上
		{ {  0.5f,  0.5f, 0.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f } }, // 右上
		{ { -0.5f, -0.5f, 0.0f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } }, // 左下

		// 2つ目の三角形 (左下 -> 右上 -> 右下)
		{ { -0.5f, -0.5f, 0.0f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } }, // 左下
		{ {  0.5f,  0.5f, 0.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f } }, // 右上
		{ {  0.5f, -0.5f, 0.0f, 1.0f }, { 1.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } }, // 右下
	};

	// テクスチャの読み込み（パスを直接指定）
	DirectX::ScratchImage mipImages2 = LoadTexture("resources/uvChecker.png"); // ※お使いのテクスチャパスに指定

	// 1. VRAM上にテクスチャ用のリソースを作成
	Microsoft::WRL::ComPtr<ID3D12Resource> textureResource;
	textureResource.Attach(CreateTextureResource(device.Get(), mipImages2.GetMetadata()));

	// 2. 中間リソースを作成し、コマンドリストに転送命令を記録
	Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource;
	intermediateResource.Attach(UploadTextureData(textureResource.Get(), mipImages2, device.Get(), commandList.Get()));

	// --- 平面用頂点バッファの作成 ---
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource;
	vertexResource.Attach(CreateBufferResource(device.Get(), sizeof(VertexData) * planeVertices.size()));

	D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
	vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();
	vertexBufferView.SizeInBytes = UINT(sizeof(VertexData) * planeVertices.size());
	vertexBufferView.StrideInBytes = sizeof(VertexData);

	// 頂点データのコピー
	VertexData* vertexData = nullptr;
	vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
	std::memcpy(vertexData, planeVertices.data(), sizeof(VertexData) * planeVertices.size());
	vertexResource->Unmap(0, nullptr);

	// --- スプライト用マテリアル ---
	Microsoft::WRL::ComPtr<ID3D12Resource> materialResourceSprite;
	materialResourceSprite.Attach(CreateBufferResource(device.Get(), (sizeof(Material) + 0xff) & ~0xff));

	Material* materialDataSprite = nullptr;
	materialResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&materialDataSprite));
	materialDataSprite->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	materialDataSprite->enableLighting = false;
	materialDataSprite->uvTransform = MakeIdentity4x4();

	// --- OBJモデル用マテリアル（重複していた宣言をここに集約） ---
	Microsoft::WRL::ComPtr<ID3D12Resource> materialResource;
	materialResource.Attach(CreateBufferResource(device.Get(), (sizeof(Material) + 0xff) & ~0xff));

	Material* materialData = nullptr;
	materialResource->Map(0, nullptr, reinterpret_cast<void**>(&materialData));
	materialData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	materialData->enableLighting = true;
	materialData->lightingType = 1;
	materialData->uvTransform = MakeIdentity4x4();

	// --- OBJモデル用WVP（重複していた宣言をここに集約） ---
	Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource;
	wvpResource.Attach(CreateBufferResource(device.Get(), 256));

	TransformationMatrix* wvpData = nullptr;
	wvpResource->Map(0, nullptr, reinterpret_cast<void**>(&wvpData));
	wvpData->WVP = MakeIdentity4x4();
	wvpData->World = MakeIdentity4x4();

	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResourceSprite;
	vertexResourceSprite.Attach(CreateBufferResource(device.Get(), sizeof(VertexData) * 6));

	D3D12_VERTEX_BUFFER_VIEW vertexBufferViewSprite{};
	vertexBufferViewSprite.BufferLocation = vertexResourceSprite->GetGPUVirtualAddress();
	vertexBufferViewSprite.SizeInBytes = sizeof(VertexData) * 6;
	vertexBufferViewSprite.StrideInBytes = sizeof(VertexData);

	VertexData* vertexDataSprite = nullptr;
	vertexResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&vertexDataSprite));

	Microsoft::WRL::ComPtr<ID3D12Resource> modelMaterialResource;
	modelMaterialResource.Attach(CreateBufferResource(device.Get(), (sizeof(Material) + 0xff) & ~0xff));

	Material* modelMaterialData = nullptr;
	modelMaterialResource->Map(0, nullptr, reinterpret_cast<void**>(&modelMaterialData));
	modelMaterialData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	modelMaterialData->enableLighting = true;
	modelMaterialData->lightingType = 1;
	modelMaterialData->uvTransform = MakeIdentity4x4();

	Microsoft::WRL::ComPtr<ID3D12Resource> modelWvpResource;
	modelWvpResource.Attach(CreateBufferResource(device.Get(), 256));

	TransformationMatrix* modelWvpData = nullptr;
	modelWvpResource->Map(0, nullptr, reinterpret_cast<void**>(&modelWvpData));
	modelWvpData->WVP = MakeIdentity4x4();
	modelWvpData->World = MakeIdentity4x4();

	// モデルの読み込みとメッシュごとの頂点バッファ生成
	// ※ フォルダ名・ファイル名はお手元の環境に合わせて変更してください
	ModelData modelData = LoadObjFile("resources", "fence.obj");
	ModelResource modelResource;

	for (const auto& meshData : modelData.meshes) {
		MeshResource meshRes{};
		meshRes.vertexBufferResource.Attach(CreateBufferResource(device.Get(), sizeof(VertexData) * meshData.vertices.size()));

		meshRes.vertexBufferView.BufferLocation = meshRes.vertexBufferResource->GetGPUVirtualAddress();
		meshRes.vertexBufferView.SizeInBytes = UINT(sizeof(VertexData) * meshData.vertices.size());
		meshRes.vertexBufferView.StrideInBytes = sizeof(VertexData);

		VertexData* mappedVertexData = nullptr;
		meshRes.vertexBufferResource->Map(0, nullptr, reinterpret_cast<void**>(&mappedVertexData));
		std::memcpy(mappedVertexData, meshData.vertices.data(), sizeof(VertexData) * meshData.vertices.size());
		meshRes.vertexBufferResource->Unmap(0, nullptr);

		modelResource.meshes.push_back(meshRes);
	}

	// モデル操作用のTransform定義（これをメインループの手前に追加）
	Transform modelTransform{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };

	// --- スプライト用頂点データ（単位サイズ化） ---
	vertexDataSprite[0].position = { 0.0f, 1.0f, 0.0f, 1.0f }; // 左下
	vertexDataSprite[0].texcoord = { 0.0f, 1.0f };
	vertexDataSprite[0].normal = { 0.0f, 0.0f, -1.0f };

	vertexDataSprite[1].position = { 0.0f, 0.0f, 0.0f, 1.0f }; // 左上
	vertexDataSprite[1].texcoord = { 0.0f, 0.0f };
	vertexDataSprite[1].normal = { 0.0f, 0.0f, -1.0f };

	vertexDataSprite[2].position = { 1.0f, 1.0f, 0.0f, 1.0f }; // 右下
	vertexDataSprite[2].texcoord = { 1.0f, 1.0f };
	vertexDataSprite[2].normal = { 0.0f, 0.0f, -1.0f };

	vertexDataSprite[3].position = { 0.0f, 0.0f, 0.0f, 1.0f }; // 左上
	vertexDataSprite[3].texcoord = { 0.0f, 0.0f };
	vertexDataSprite[3].normal = { 0.0f, 0.0f, -1.0f };

	vertexDataSprite[4].position = { 1.0f, 0.0f, 0.0f, 1.0f }; // 右上
	vertexDataSprite[4].texcoord = { 1.0f, 0.0f };
	vertexDataSprite[4].normal = { 0.0f, 0.0f, -1.0f };

	vertexDataSprite[5].position = { 1.0f, 1.0f, 0.0f, 1.0f }; // 右下
	vertexDataSprite[5].texcoord = { 1.0f, 1.0f };
	vertexDataSprite[5].normal = { 0.0f, 0.0f, -1.0f };

	Microsoft::WRL::ComPtr<ID3D12Resource> indexResourceSprite;
	indexResourceSprite.Attach(CreateBufferResource(device.Get(), sizeof(uint32_t) * 6));

	uint32_t* indexDataSprite = nullptr;
	indexResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&indexDataSprite));
	indexDataSprite[0] = 0; indexDataSprite[1] = 1; indexDataSprite[2] = 2;
	indexDataSprite[3] = 1; indexDataSprite[4] = 3; indexDataSprite[5] = 2;
	indexResourceSprite->Unmap(0, nullptr);

	D3D12_INDEX_BUFFER_VIEW indexBufferViewSprite{};
	indexBufferViewSprite.BufferLocation = indexResourceSprite->GetGPUVirtualAddress();
	indexBufferViewSprite.SizeInBytes = sizeof(uint32_t) * 6;
	indexBufferViewSprite.Format = DXGI_FORMAT_R32_UINT;

	Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource;
	directionalLightResource.Attach(CreateBufferResource(device.Get(), sizeof(DirectionalLight)));

	DirectionalLight* directionalLightData = nullptr;
	directionalLightResource->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData));
	directionalLightData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	directionalLightData->direction = { 0.0f, -1.0f, 0.0f };
	directionalLightData->intensity = 1.0f;

	// --- 球用の頂点リソースを作成 ---
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResourceSphere;
	vertexResourceSphere.Attach(CreateBufferResource(device.Get(), sizeof(VertexData) * sphereVertices.size()));

	// --- 球用の頂点バッファビュー(VBV)を定義 ---
	D3D12_VERTEX_BUFFER_VIEW vertexBufferViewSphere{};
	vertexBufferViewSphere.BufferLocation = vertexResourceSphere->GetGPUVirtualAddress();
	vertexBufferViewSphere.SizeInBytes = UINT(sizeof(VertexData) * sphereVertices.size());
	vertexBufferViewSphere.StrideInBytes = sizeof(VertexData);

	// --- 球の頂点データをリソースに書き込む ---
	VertexData* vertexDataSphere = nullptr;
	vertexResourceSphere->Map(0, nullptr, reinterpret_cast<void**>(&vertexDataSphere));
	std::memcpy(vertexDataSphere, sphereVertices.data(), sizeof(VertexData) * sphereVertices.size());
	vertexResourceSphere->Unmap(0, nullptr);

	const uint32_t kIndexCountSphere = kSubdivision * kSubdivision * 6;
	Microsoft::WRL::ComPtr<ID3D12Resource> indexResourceSphere;
	indexResourceSphere.Attach(CreateBufferResource(device.Get(), sizeof(uint32_t) * kIndexCountSphere));

	uint32_t* indexDataSphere = nullptr;
	indexResourceSphere->Map(0, nullptr, reinterpret_cast<void**>(&indexDataSphere));
	uint32_t indexOffset = 0;
	for (uint32_t i = 0; i < kIndexCountSphere; ++i) {
		indexDataSphere[i] = i + indexOffset;
	}
	indexResourceSphere->Unmap(0, nullptr);

	D3D12_INDEX_BUFFER_VIEW indexBufferViewSphere{};
	indexBufferViewSphere.BufferLocation = indexResourceSphere->GetGPUVirtualAddress();
	indexBufferViewSphere.SizeInBytes = sizeof(uint32_t) * kIndexCountSphere;
	indexBufferViewSphere.Format = DXGI_FORMAT_R32_UINT;

	const DirectX::TexMetadata& metadata2 = mipImages2.GetMetadata();

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = metadata2.format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = static_cast<UINT>(metadata2.mipLevels);

	D3D12_CPU_DESCRIPTOR_HANDLE srvHandleCPU = GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 1);
	D3D12_GPU_DESCRIPTOR_HANDLE srvHandleGPU = GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 1);
	device->CreateShaderResourceView(textureResource.Get(), &srvDesc, srvHandleCPU);

	D3D12_SAMPLER_DESC samplerDesc{};
	samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> samplerDescriptorHeap;
	samplerDescriptorHeap.Attach(CreateDescriptorHeap(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1, false));
	D3D12_CPU_DESCRIPTOR_HANDLE samplerHandleCPU = samplerDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	device->CreateSampler(&samplerDesc, samplerHandleCPU);

	Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain;
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
	swapChainDesc.Width = kClientWidth;
	swapChainDesc.Height = kClientHeight;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = 2;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

	hr = dxgiFactory->CreateSwapChainForHwnd(
		commandQueue.Get(),
		hwnd,
		&swapChainDesc,
		nullptr,
		nullptr,
		reinterpret_cast<IDXGISwapChain1**>(swapChain.GetAddressOf())
	);
	assert(SUCCEEDED(hr));

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap;
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.NumDescriptors = 2;

	hr = device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvDescriptorHeap));
	assert(SUCCEEDED(hr));

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap;
	dsvDescriptorHeap.Attach(CreateDescriptorHeap(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false));

	Microsoft::WRL::ComPtr<ID3D12Resource> depthStencilResource;
	depthStencilResource.Attach(CreateDepthStencilTextureResource(device.Get(), kClientWidth, kClientHeight));

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	device->CreateDepthStencilView(depthStencilResource.Get(), &dsvDesc, dsvHandle);

	Microsoft::WRL::ComPtr<ID3D12Resource> swapChainResources[2];
	hr = swapChain->GetBuffer(0, IID_PPV_ARGS(&swapChainResources[0]));
	assert(SUCCEEDED(hr));
	hr = swapChain->GetBuffer(1, IID_PPV_ARGS(&swapChainResources[1]));
	assert(SUCCEEDED(hr));

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2];
	rtvHandles[0] = GetCPUDescriptorHandle(rtvDescriptorHeap.Get(), descriptorSizeRTV, 0);
	rtvHandles[1] = GetCPUDescriptorHandle(rtvDescriptorHeap.Get(), descriptorSizeRTV, 1);

	device->CreateRenderTargetView(swapChainResources[0].Get(), &rtvDesc, rtvHandles[0]);
	device->CreateRenderTargetView(swapChainResources[1].Get(), &rtvDesc, rtvHandles[1]);

#ifdef USE_IMGUI
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();

	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX12_Init(
		device.Get(),
		swapChainDesc.BufferCount,
		rtvDesc.Format,
		srvDescriptorHeap.Get(),
		srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart()
	);

	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->Build();
#endif

	Microsoft::WRL::ComPtr<ID3D12Resource> transformationMatrixResourceSprite;
	transformationMatrixResourceSprite.Attach(CreateBufferResource(device.Get(), 256));

	Matrix4x4* transformationMatrixDataSprite = nullptr;
	transformationMatrixResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixDataSprite));
	*transformationMatrixDataSprite = MakeIdentity4x4();

	// --- 球体専用のマテリアルリソース作成 ---
	Microsoft::WRL::ComPtr<ID3D12Resource> materialResourceSphere;
	materialResourceSphere.Attach(CreateBufferResource(device.Get(), (sizeof(Material) + 0xff) & ~0xff));

	Material* materialDataSphere = nullptr;
	materialResourceSphere->Map(0, nullptr, reinterpret_cast<void**>(&materialDataSphere));
	materialDataSphere->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	materialDataSphere->enableLighting = true;
	materialDataSphere->lightingType = 1; // 初期値（1: ランバート反射、0: 無し、2: ハーフランバート）
	materialDataSphere->uvTransform = MakeIdentity4x4();

	// --- 各物体の初期位置設定（重なりを防ぐために左右に配置）
	Transform sphereTransform{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {-1.2f, 0.0f, 0.0f} }; // 球体 (左に1.2)

	// 2D Sprite (画面左上付近に配置)
	Transform transformSprite{ {200.0f, 200.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {100.0f, 100.0f, 0.0f} };
	Transform uvTransformSprite{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };

	Microsoft::WRL::ComPtr<ID3D12Resource> wvpResourceSphere;
	wvpResourceSphere.Attach(CreateBufferResource(device.Get(), 256));

	TransformationMatrix* wvpDataSphere = nullptr;
	wvpResourceSphere->Map(0, nullptr, reinterpret_cast<void**>(&wvpDataSphere));

	DirectX::ScratchImage mipImagesSpriteDefault = LoadTexture("resources/uvChecker.png");

	// --- スプライト用テクスチャの作成と転送 (UploadTextureData を使用) ---
	Microsoft::WRL::ComPtr<ID3D12Resource> textureResourceSprite;
	textureResourceSprite.Attach(CreateTextureResource(device.Get(), mipImagesSpriteDefault.GetMetadata()));

	Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResourceSprite;
	intermediateResourceSprite.Attach(UploadTextureData(textureResourceSprite.Get(), mipImagesSpriteDefault, device.Get(), commandList.Get()));

	// --- スプライト用 SRV の作成 ---
	D3D12_CPU_DESCRIPTOR_HANDLE srvHandleCPUSprite = GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 2);
	D3D12_GPU_DESCRIPTOR_HANDLE srvHandleGPUSprite = GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 2);

	// SRVのDescを設定（スプライト用のフォーマット・MipLevelsを使用）
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDescSprite{};
	srvDescSprite.Format = mipImagesSpriteDefault.GetMetadata().format;
	srvDescSprite.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDescSprite.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDescSprite.Texture2D.MipLevels = static_cast<UINT>(mipImagesSpriteDefault.GetMetadata().mipLevels);

	device->CreateShaderResourceView(textureResourceSprite.Get(), &srvDescSprite, srvHandleCPUSprite);

	// フェンス用テクスチャの読み込みとSRV作成
	DirectX::ScratchImage mipImagesFence = LoadTexture("resources/fence.png");

	Microsoft::WRL::ComPtr<ID3D12Resource> textureResourceFence;
	textureResourceFence.Attach(CreateTextureResource(device.Get(), mipImagesFence.GetMetadata()));

	Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResourceFence;
	intermediateResourceFence.Attach(UploadTextureData(textureResourceFence.Get(), mipImagesFence, device.Get(), commandList.Get()));

	// ディスクリプタヒープの 3 番にフェンス用 SRV を作成
	D3D12_CPU_DESCRIPTOR_HANDLE srvHandleCPUFence = GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 3);
	D3D12_GPU_DESCRIPTOR_HANDLE srvHandleGPUFence = GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 3);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDescFence{};
	srvDescFence.Format = mipImagesFence.GetMetadata().format;
	srvDescFence.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDescFence.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDescFence.Texture2D.MipLevels = static_cast<UINT>(mipImagesFence.GetMetadata().mipLevels);

	device->CreateShaderResourceView(textureResourceFence.Get(), &srvDescFence, srvHandleCPUFence);

	// =========================================================
	// Instancing用 Resource と SRV の作成
	// =========================================================
	const uint32_t kNumInstance = 10; // インスタンス数

	// Instancing用に最大数分のTransformを用意
	Transform transforms[kNumInstance];
	for (uint32_t index = 0; index < kNumInstance; ++index) {
		transforms[index].scale = { 1.0f, 1.0f, 1.0f };
		transforms[index].rotate = { 0.0f, 0.0f, 0.0f };
		transforms[index].translate = { index * 0.1f, index * 0.1f, index * 0.1f }; // 位置をずらす
	}

	// 1. Resourceの作成と初期化
	Microsoft::WRL::ComPtr<ID3D12Resource> instancingResource;
	instancingResource.Attach(CreateBufferResource(device.Get(), sizeof(TransformationMatrix) * kNumInstance));

	TransformationMatrix* instancingData = nullptr;
	instancingResource->Map(0, nullptr, reinterpret_cast<void**>(&instancingData));

	for (uint32_t index = 0; index < kNumInstance; ++index) {
		instancingData[index].WVP = MakeIdentity4x4();
		instancingData[index].World = MakeIdentity4x4();
	}

	// 2. StructuredBuffer用 SRV の作成
	D3D12_SHADER_RESOURCE_VIEW_DESC instancingSrvDesc{};
	instancingSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
	instancingSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	instancingSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	instancingSrvDesc.Buffer.FirstElement = 0;
	instancingSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
	instancingSrvDesc.Buffer.NumElements = kNumInstance;
	instancingSrvDesc.Buffer.StructureByteStride = sizeof(TransformationMatrix);

	D3D12_CPU_DESCRIPTOR_HANDLE instancingSrvHandleCPU = GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 4);
	D3D12_GPU_DESCRIPTOR_HANDLE instancingSrvHandleGPU = GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 4);

	device->CreateShaderResourceView(instancingResource.Get(), &instancingSrvDesc, instancingSrvHandleCPU);

	ShowWindow(hwnd, SW_SHOW);

	// 1. DirectInputオブジェクトの生成
	Microsoft::WRL::ComPtr<IDirectInput8> directInput;
	hr = DirectInput8Create(
		wc.hInstance,
		DIRECTINPUT_VERSION,
		IID_IDirectInput8,
		reinterpret_cast<void**>(directInput.GetAddressOf()),
		nullptr
	);
	assert(SUCCEEDED(hr));

	// 2. キーボードデバイスの生成
	Microsoft::WRL::ComPtr<IDirectInputDevice8> keyboard;
	hr = directInput->CreateDevice(GUID_SysKeyboard, keyboard.GetAddressOf(), nullptr);
	assert(SUCCEEDED(hr));

	// 3. 入力データ形式のセット
	hr = keyboard->SetDataFormat(&c_dfDIKeyboard);
	assert(SUCCEEDED(hr));

	// 4. 排他制御レベルのセット
	hr = keyboard->SetCooperativeLevel(
		hwnd,
		DISCL_FOREGROUND | DISCL_NONEXCLUSIVE | DISCL_NOWINKEY
	);
	assert(SUCCEEDED(hr));

	// ========================
	// 音声読み込み
	// ========================
	SoundData soundData1 = SoundLoadWave("resources/fanfare.wav");

	BYTE key[256] = {};
	BYTE keyPre[256] = {};

	MSG msg{};
	while (msg.message != WM_QUIT) {
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		} else {
			// --------------------------------------------------
			// DirectInput 毎フレーム更新処理 
			// --------------------------------------------------
			// 前フレームのキー状態を保存
			std::memcpy(keyPre, key, sizeof(key));

			// 1. キーボード情報の取得開始 (戻り値を受け取る)
			HRESULT hrAcquire = keyboard->Acquire();

			// 2. 全キーの入力状態を取得する (戻り値を受け取る)
			HRESULT hrState = keyboard->GetDeviceState(sizeof(key), key);

			// デバイスがロストしている、または取得できていない場合
			if (hrState == DIERR_INPUTLOST || hrState == DIERR_NOTACQUIRED) {
				// 再取得を試みる
				hrAcquire = keyboard->Acquire();
				if (SUCCEEDED(hrAcquire)) {
					// 再取得に成功したら、もう一度データを読み直す
					keyboard->GetDeviceState(sizeof(key), key);
				} else {
					// 完全にロストしている場合は入力をクリアして安全を保つ
					std::memset(key, 0, sizeof(key));
				}
			}

			if (key[DIK_0]) {
				OutputDebugStringA("Hit 0\n");
			}

#ifdef USE_IMGUI
			ImGui_ImplDX12_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();

			ImGui::Begin("Settings");

			// 開いておくノード（DefaultOpen）
			if (ImGui::TreeNodeEx("Rendering Switch", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::Checkbox("Draw Plane", &enablePlaneDraw);
				ImGui::Checkbox("Draw Model", &enableModelDraw);
				ImGui::Checkbox("Draw Sphere", &enableSphereDraw);
				ImGui::Checkbox("Draw Sprite", &enableSpriteDraw);
				ImGui::TreePop();
			}

			// 1. 平面 (Plane) - 開いておく
			if (ImGui::TreeNodeEx("Object (Plane)", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::ColorEdit4("Material Color", &materialData->color.x);
				ImGui::SliderFloat3("Translate", &transform.translate.x, -10.0f, 10.0f);
				ImGui::SliderFloat3("Rotate", &transform.rotate.x, -3.14f, 3.14f);
				ImGui::SliderFloat3("Scale", &transform.scale.x, 0.1f, 10.0f);
				ImGui::TreePop();
			}

			// 2. 球体 (Sphere) - 初期状態は閉じる (0 を指定)
			if (ImGui::TreeNodeEx("Object (Sphere)", 0)) {
				ImGui::ColorEdit4("Color##Sphere", &materialDataSphere->color.x);
				ImGui::SliderFloat3("Translate##Sphere", &sphereTransform.translate.x, -10.0f, 10.0f);
				ImGui::SliderFloat3("Rotate##Sphere", &sphereTransform.rotate.x, -3.14f, 3.14f);
				ImGui::SliderFloat3("Scale##Sphere", &sphereTransform.scale.x, 0.1f, 10.0f);
				ImGui::TreePop();
			}

			// 3. スプライト (Sprite) - 初期状態は閉じる (0 を指定)
			if (ImGui::TreeNodeEx("Object (Sprite)", 0)) {
				ImGui::SliderFloat3("Translate##Sprite", &transformSprite.translate.x, -500.0f, 1280.0f);
				ImGui::SliderFloat3("Rotate##Sprite", &transformSprite.rotate.x, -3.14f, 3.14f);
				ImGui::SliderFloat3("Scale##Sprite", &transformSprite.scale.x, 1.0f, 1000.0f);

				if (ImGui::TreeNode("UV Transform##Sprite")) {
					ImGui::SliderFloat2("UV Translate", &uvTransformSprite.translate.x, -10.0f, 10.0f);
					ImGui::SliderFloat("UV Rotate", &uvTransformSprite.rotate.z, -3.14f, 3.14f);
					ImGui::SliderFloat2("UV Scale", &uvTransformSprite.scale.x, 0.1f, 10.0f);
					ImGui::TreePop();
				}
				ImGui::TreePop();
			}

			// モデル (Model) - 初期状態は閉じる (0 を指定)
			if (ImGui::TreeNodeEx("Object (Model)", 0)) {
				ImGui::ColorEdit4("Color##Model", &modelMaterialData->color.x);
				ImGui::SliderFloat3("Translate##Model", &modelTransform.translate.x, -10.0f, 10.0f);
				ImGui::SliderFloat3("Rotate##Model", &modelTransform.rotate.x, -3.14f, 3.14f);
				ImGui::SliderFloat3("Scale##Model", &modelTransform.scale.x, 0.1f, 10.0f);
				ImGui::TreePop();
			}

			// 4. ライト (Light) - 開いておく (DefaultOpen)
			if (ImGui::TreeNodeEx("Light", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::ColorEdit4("LightColor", &directionalLightData->color.x);
				ImGui::SliderFloat3("LightDirection", &directionalLightData->direction.x, -1.0f, 1.0f);
				ImGui::SliderFloat("Intensity", &directionalLightData->intensity, 0.0f, 10.0f);

				static int currentLightingType = 1;

				const char* lightingItems[] = { "None", "Lambert", "Half Lambert" };
				if (ImGui::Combo("Lighting Type", &currentLightingType, lightingItems, IM_ARRAYSIZE(lightingItems))) {
					materialData->lightingType = currentLightingType;
					materialDataSphere->lightingType = currentLightingType;
					modelMaterialData->lightingType = currentLightingType;
				}

				ImGui::TreePop();
			}

			// 5. サウンドコントロール - 初期状態は閉じる (0 を指定)
			if (ImGui::TreeNodeEx("Sound Control", 0)) {
				if (ImGui::Button("Play Fanfare")) {
					SoundPlayWave(xAudio2.Get(), soundData1);
				}
				ImGui::TreePop();
			}

			ImGui::End();
#endif

			Matrix4x4 cameraMatrix = Matrix4x4::MakeAffineMatrix(cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate);
			Matrix4x4 viewMatrix = Matrix4x4::Inverse(cameraMatrix);
			Matrix4x4 projectionMatrix = Matrix4x4::MakePerspectiveFovMatrix(0.45f, float(kClientWidth) / float(kClientHeight), 0.01f, 100.0f);

			// ★【追加】ビュープロジェクション行列の計算 (viewProjectionMatrix の定義)
			Matrix4x4 viewProjectionMatrix = Matrix4x4::Multiply(viewMatrix, projectionMatrix);

			// 平面 (Plane) の行列計算
			Matrix4x4 worldMatrix = Matrix4x4::MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
			wvpData->WVP = Matrix4x4::Multiply(worldMatrix, viewProjectionMatrix);
			wvpData->World = worldMatrix;

			// =========================================================
			// Instancing用の行列計算・データ更新 
			// =========================================================
			for (uint32_t index = 0; index < kNumInstance; ++index) {
				Matrix4x4 worldMat = Matrix4x4::MakeAffineMatrix(transforms[index].scale, transforms[index].rotate, transforms[index].translate);
				Matrix4x4 worldViewProjectionMatrix = Matrix4x4::Multiply(worldMat, viewProjectionMatrix);

				instancingData[index].WVP = worldViewProjectionMatrix;
				instancingData[index].World = worldMat;
			}

			// 球体 (Sphere) の行列計算
			Matrix4x4 worldMatrixSphere = Matrix4x4::MakeAffineMatrix(sphereTransform.scale, sphereTransform.rotate, sphereTransform.translate);
			wvpDataSphere->WVP = Matrix4x4::Multiply(worldMatrixSphere, viewProjectionMatrix);
			wvpDataSphere->World = worldMatrixSphere;

			Matrix4x4 worldMatrixModel = Matrix4x4::MakeAffineMatrix(modelTransform.scale, modelTransform.rotate, modelTransform.translate);
			modelWvpData->WVP = Matrix4x4::Multiply(worldMatrixModel, viewProjectionMatrix);
			modelWvpData->World = worldMatrixModel;

			// --- スプライトの行列計算（そのまま残す） ---
			Matrix4x4 worldMatrixSprite = Matrix4x4::MakeAffineMatrix(transformSprite.scale, transformSprite.rotate, transformSprite.translate);
			Matrix4x4 viewMatrixSprite = MakeIdentity4x4();
			Matrix4x4 projectionMatrixSprite = Matrix4x4::MakeOrthographicMatrix(0.0f, 0.0f, float(kClientWidth), float(kClientHeight), 0.0f, 100.0f);
			*transformationMatrixDataSprite = Matrix4x4::Multiply(worldMatrixSprite, Matrix4x4::Multiply(viewMatrixSprite, projectionMatrixSprite));

			Matrix4x4 uvScaleMat = Matrix4x4::MakeScaleMatrix(uvTransformSprite.scale);
			Matrix4x4 uvRotMat = Matrix4x4::MakeRotateZMatrix(uvTransformSprite.rotate.z);
			Matrix4x4 uvTransMat = Matrix4x4::MakeTranslateMatrix(uvTransformSprite.translate);
			Matrix4x4 uvTransformMatrix = Matrix4x4::Multiply(uvScaleMat, Matrix4x4::Multiply(uvRotMat, uvTransMat));

			materialDataSprite->uvTransform = uvTransformMatrix;

#ifdef USE_IMGUI
			ImGui::Render();
#endif
			UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();

			D3D12_RESOURCE_BARRIER barrier{};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			barrier.Transition.pResource = swapChainResources[backBufferIndex].Get();
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

			ID3D12DescriptorHeap* descriptorHeaps[] = { srvDescriptorHeap.Get() };
			commandList->SetDescriptorHeaps(1, descriptorHeaps);

			// =========================================================
			// Particle (Instancing) の描画 [スライド12]
			// =========================================================
			commandList->SetPipelineState(particlePipelineState.Get());
			commandList->SetGraphicsRootSignature(particleRootSignature.Get());

			// ルートパラメータの設定
			commandList->SetGraphicsRootConstantBufferView(0, materialResource->GetGPUVirtualAddress()); // Material
			commandList->SetGraphicsRootDescriptorTable(1, instancingSrvHandleGPU);                       // Instancing SRV [スライド12]
			commandList->SetGraphicsRootDescriptorTable(2, srvHandleGPU);                            // Texture SRV

			// 描画インスタンス数が確保数(kNumInstance)を超えないかチェック
			assert(kNumInstance <= 10);

			// 平面メッシュの頂点バッファを使って Instancing 描画を発行 
			commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
			commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			commandList->DrawInstanced(UINT(planeVertices.size()), kNumInstance, 0, 0);

			commandList->SetPipelineState(graphicsPipelineState.Get());
			commandList->SetGraphicsRootSignature(rootSignature.Get());

			// --- 平面 (Plane) の描画 ---
			if (enablePlaneDraw) {
				commandList->SetGraphicsRootConstantBufferView(0, materialResource->GetGPUVirtualAddress());
				commandList->SetGraphicsRootConstantBufferView(1, wvpResource->GetGPUVirtualAddress());
				commandList->SetGraphicsRootDescriptorTable(2, srvHandleGPU);
				commandList->SetGraphicsRootConstantBufferView(3, directionalLightResource->GetGPUVirtualAddress());

				commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
				commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				commandList->DrawInstanced(UINT(planeVertices.size()), 1, 0, 0);
			}

			// --- 球体 (Sphere) の描画 ---
			if (enableSphereDraw) {
				commandList->SetGraphicsRootConstantBufferView(0, materialResourceSphere->GetGPUVirtualAddress());
				commandList->SetGraphicsRootConstantBufferView(1, wvpResourceSphere->GetGPUVirtualAddress());
				commandList->SetGraphicsRootDescriptorTable(2, srvHandleGPU);
				commandList->SetGraphicsRootConstantBufferView(3, directionalLightResource->GetGPUVirtualAddress());

				commandList->IASetVertexBuffers(0, 1, &vertexBufferViewSphere);
				commandList->IASetIndexBuffer(&indexBufferViewSphere);
				commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				commandList->DrawIndexedInstanced(kIndexCountSphere, 1, 0, 0, 0);
			}

			if (enableModelDraw) {
				// フェンス描画前に非加算合成（通常アルファ）用 PSO をセット
				commandList->SetPipelineState(graphicsPipelineStateFence.Get());

				commandList->SetGraphicsRootConstantBufferView(0, modelMaterialResource->GetGPUVirtualAddress());
				commandList->SetGraphicsRootConstantBufferView(1, modelWvpResource->GetGPUVirtualAddress());
				commandList->SetGraphicsRootDescriptorTable(2, srvHandleGPUFence);
				commandList->SetGraphicsRootConstantBufferView(3, directionalLightResource->GetGPUVirtualAddress());

				for (size_t i = 0; i < modelResource.meshes.size(); ++i) {
					commandList->IASetVertexBuffers(0, 1, &modelResource.meshes[i].vertexBufferView);
					commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
					commandList->DrawInstanced(UINT(modelData.meshes[i].vertices.size()), 1, 0, 0);
				}
			}

			// --- スプライト (Sprite) の描画 ---
			if (enableSpriteDraw) {
				commandList->SetGraphicsRootConstantBufferView(0, materialResourceSprite->GetGPUVirtualAddress());
				commandList->SetGraphicsRootConstantBufferView(1, transformationMatrixResourceSprite->GetGPUVirtualAddress());
				commandList->SetGraphicsRootDescriptorTable(2, srvHandleGPUSprite);

				commandList->IASetVertexBuffers(0, 1, &vertexBufferViewSprite);
				commandList->IASetIndexBuffer(&indexBufferViewSprite);
				commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

				commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);
			}

#ifdef USE_IMGUI
			ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList.Get());
#endif

			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
			commandList->ResourceBarrier(1, &barrier);

			hr = commandList->Close();
			assert(SUCCEEDED(hr));

			// ★【修正】アスタリスク '*' を追加
			ID3D12CommandList* commandLists[] = { commandList.Get() };
			commandQueue->ExecuteCommandLists(1, commandLists);

			swapChain->Present(1, 0);

			fenceValue++;
			hr = commandQueue->Signal(fence.Get(), fenceValue);
			assert(SUCCEEDED(hr));

			if (fence->GetCompletedValue() < fenceValue) {
				hr = fence->SetEventOnCompletion(fenceValue, fenceEvent);
				assert(SUCCEEDED(hr));
				WaitForSingleObject(fenceEvent, INFINITE);
			}

			commandAllocator->Reset();
			commandList->Reset(commandAllocator.Get(), nullptr);
		}
	}

	// ========================
	// 解放処理
	// ========================

	// 1. 先にXAudio2を解放して再生スレッドを安全に停止
	xAudio2.Reset();

	// 2. その後に音声データのメモリを安全に解放
	SoundUnload(&soundData1);

#ifdef USE_IMGUI
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
#endif

	// OSのハンドルなどは手動で閉じる
	if (fenceEvent) {
		CloseHandle(fenceEvent);
	}

	return 0;
}