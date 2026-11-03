#pragma once
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>
#include <array>
#include <chrono>
#include <cstdint>
#include "externals/DirectXTex/DirectXTex.h"

class WinApp;

// DirectX12 の基盤部分をまとめたクラス
class DirectXCommon
{
public:
	template <class T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	static constexpr uint32_t kBackBufferCount = 2;

	~DirectXCommon();

	// 初期化（デバイス〜スワップチェーン〜フェンスまで一括）
	void Initialize(WinApp* winApp);

	// 毎フレームの描画前処理（バリア・クリア・ビューポート等）
	void PreDraw();
	// 毎フレームの描画後処理（Present・GPU待ち・FPS固定）
	void PostDraw();

	// ---- Getter（ComPtr から普通のポインタを取り出して返す）----
	ID3D12Device* GetDevice() const { return device_.Get(); }
	ID3D12GraphicsCommandList* GetCommandList() const { return commandList_.Get(); }
	ID3D12DescriptorHeap* GetSrvDescriptorHeap() const { return srvDescriptorHeap_.Get(); }
	uint32_t GetDescriptorSizeSRV() const { return descriptorSizeSRV_; }
	uint32_t GetBackBufferCount() const { return kBackBufferCount; }

	// ---- リソース作成ヘルパー ----
	ComPtr<ID3D12Resource> CreateBufferResource(size_t sizeInBytes);
	ComPtr<ID3D12Resource> CreateTextureResource(const DirectX::TexMetadata& metadata);
	// 戻り値の中間リソースは、転送が終わる(最初のPostDraw後)まで保持すること
	[[nodiscard]]
	ComPtr<ID3D12Resource> UploadTextureData(ID3D12Resource* texture, const DirectX::ScratchImage& mipImages);
	ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors, bool shaderVisible);

	static D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index);
	static D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index);

private:
	void InitializeDevice();
	void InitializeCommand();
	void InitializeSwapChain();
	void InitializeDescriptorHeaps();
	void InitializeRenderTargetView();
	void InitializeDepthStencilView();
	void InitializeFence();
	void InitializeFixFPS();

	void WaitForGpu();
	void UpdateFixFPS();

	ComPtr<ID3D12Resource> CreateDepthStencilTextureResource(int32_t width, int32_t height);

private:
	WinApp* winApp_ = nullptr;

	ComPtr<ID3D12Debug1> debugController_;
	ComPtr<IDXGIFactory7> dxgiFactory_;
	ComPtr<IDXGIAdapter4> useAdapter_;
	ComPtr<ID3D12Device> device_;

	ComPtr<ID3D12CommandQueue> commandQueue_;
	ComPtr<ID3D12CommandAllocator> commandAllocator_;
	ComPtr<ID3D12GraphicsCommandList> commandList_;

	ComPtr<IDXGISwapChain4> swapChain_;
	std::array<ComPtr<ID3D12Resource>, kBackBufferCount> swapChainResources_;

	ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap_;
	ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap_;
	ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap_;
	ComPtr<ID3D12Resource> depthStencilResource_;

	std::array<D3D12_CPU_DESCRIPTOR_HANDLE, kBackBufferCount> rtvHandles_{};
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle_{};

	uint32_t descriptorSizeSRV_ = 0;
	uint32_t descriptorSizeRTV_ = 0;
	uint32_t descriptorSizeDSV_ = 0;

	ComPtr<ID3D12Fence> fence_;
	uint64_t fenceValue_ = 0;
	HANDLE fenceEvent_ = nullptr;

	UINT backBufferIndex_ = 0;

	// FPS固定用: 前回フレームの記録時間
	std::chrono::steady_clock::time_point reference_;
};
