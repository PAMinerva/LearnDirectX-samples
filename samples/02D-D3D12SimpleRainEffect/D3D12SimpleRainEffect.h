//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#pragma once

#include "DXSample.h"
#include "StepTimer.h"

using namespace DirectX;

// Note that while ComPtr is used to manage the lifetime of resources on the CPU,
// it has no understanding of the lifetime of resources on the GPU. Apps must account
// for the GPU lifetime of resources to avoid destroying objects that may still be
// referenced by the GPU.
// An example of this can be found in the class method: OnDestroy().
using Microsoft::WRL::ComPtr;

class D3D12SimpleRainEffect : public DXSample
{
public:
    D3D12SimpleRainEffect(UINT width, UINT height, std::wstring name);

    virtual void OnInit();
    virtual void OnUpdate();
    virtual void OnRender();
    virtual void OnDestroy();

private:
    // In this sample we overload the meaning of FrameCount to mean both the maximum
    // number of frames that will be queued to the GPU at a time, as well as the number
    // of back buffers in the DXGI swap chain. For the majority of applications, this
    // is convenient and works well. However, there will be certain cases where an
    // application may want to queue up more frames than there are back buffers
    // available.
    // It should be noted that excessive buffering of frames dependent on user input
    // may result in noticeable latency in your app.
    static const UINT FrameCount = 2;

    // Vertex attributes
    struct Vertex
    {
        XMFLOAT3 position;
        XMFLOAT2 size;
        FLOAT speed;
    };

    // Constant buffer
    struct ConstantBuffer
    {
        XMFLOAT4X4 worldMatrix;        // 64 bytes
        XMFLOAT4X4 viewMatrix;         // 64 bytes
        XMFLOAT4X4 projectionMatrix;   // 64 bytes
        XMFLOAT4 outputColor;          // 16 bytes
        XMFLOAT3 cameraWPos;           // 12 bytes
        FLOAT deltaTime;               //  4 bytes
    };

    // We'll allocate space for several of these and they will need to be padded for alignment.
    static_assert(sizeof(ConstantBuffer) == 224, "Checking the size here.");

    // D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT < 272 < 2 * D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT
    // Create a union with the correct size and enough room for one ConstantBuffer
    union PaddedConstantBuffer
    {
        ConstantBuffer constants;
        uint8_t bytes[D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT];
    };

    // Check the exact size of the PaddedConstantBuffer to make sure it will align properly
    static_assert(sizeof(PaddedConstantBuffer) == D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, "PaddedConstantBuffer is not aligned properly");

    // Pipeline objects.
    CD3DX12_VIEWPORT m_viewport;
    CD3DX12_RECT m_scissorRect;
    ComPtr<IDXGISwapChain3> m_swapChain;
    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12Resource> m_renderTargets[FrameCount];
    ComPtr<ID3D12Resource> m_depthStencil;
    ComPtr<ID3D12CommandAllocator> m_commandAllocators[FrameCount];
    ComPtr<ID3D12CommandQueue> m_commandQueue;
    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
    ComPtr<ID3D12PipelineState>  m_streamPipelineState;
    ComPtr<ID3D12PipelineState>  m_pipelineState;
    ComPtr<ID3D12GraphicsCommandList> m_commandList;

    // App resources.
    ComPtr<ID3D12Resource> m_vertexBuffer;
    ComPtr<ID3D12Resource> m_indexBuffer;
    ComPtr<ID3D12Resource> m_perFrameConstants;
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
    D3D12_INDEX_BUFFER_VIEW m_indexBufferView;
    D3D12_GPU_VIRTUAL_ADDRESS m_constantDataGpuAddr;
    PaddedConstantBuffer* m_mappedConstantData;
    UINT m_rtvDescriptorSize;

    StepTimer m_timer;

    // Synchronization objects.
    UINT m_frameIndex;
    HANDLE m_fenceEvent;
    ComPtr<ID3D12Fence> m_fence;
    UINT64 m_fenceValues[FrameCount];

    // Scene constants, updated per-frame
    float m_curRotationAngleRad;

    // In this simple sample, we know that there are three draw calls
    // and we will update the scene constants for each draw call.
    static const unsigned int c_numDrawCalls = 2;

    // These computed values will be loaded into a ConstantBuffer
    XMMATRIX m_worldMatrix;
    XMMATRIX m_viewMatrix;
    XMMATRIX m_projectionMatrix;
    XMVECTOR m_outputColor;
    XMVECTOR m_cameraWPos;

    void LoadPipeline();
    void LoadAssets();
    void PopulateCommandList();
    void MoveToNextFrame();
    void WaitForGpu();

    // Particle collection
    std::vector<Vertex> particleVertices;

    // Streaming resources
    ComPtr<ID3D12Resource>			m_streamOutputBuffer;
    D3D12_STREAM_OUTPUT_BUFFER_VIEW m_streamOutputBufferView;
    ComPtr<ID3D12Resource>			m_streamFilledSizeBuffer;
    ComPtr<ID3D12Resource>			m_streamFilledSizeUploadBuffer;
    ComPtr<ID3D12Resource>			m_streamFilledSizeReadBackBuffer;
    ComPtr<ID3D12Resource>			m_updatedVertexBuffer;
    UINT* m_pFilledSize;
};