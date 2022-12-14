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

#include "stdafx.h"
#include "D3D12Stenciling.h"


D3D12Stenciling::D3D12Stenciling(UINT width, UINT height, std::wstring name) :
    DXSample(width, height, name),
    m_viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)),
    m_scissorRect(0, 0, static_cast<LONG>(width), static_cast<LONG>(height)),
    m_constantDataGpuAddr(0),
    m_mappedConstantData(nullptr),
    m_rtvDescriptorSize(0),
    m_frameIndex(0),
    m_fenceValues{},
    m_curRotationAngleRad(0.0f)
{
    // Initialize the world matrix of the cube
    m_cubeWorldMatrix = XMMatrixIdentity();

    // Initialize the view matrix
    static const XMVECTORF32 c_eye = { 3.0f, 4.0f, -10.0f, 0.0f };
    static const XMVECTORF32 c_at = { 0.0f, 0.0f, 0.0f, 0.0f };
    static const XMVECTORF32 c_up = { 0.0f, 1.0f, 0.0f, 0.0 };
    m_viewMatrix = XMMatrixLookAtLH(c_eye, c_at, c_up);

    // Initialize the projection matrix
    m_projectionMatrix = XMMatrixPerspectiveFovLH(XM_PIDIV4, width / (FLOAT)height, 0.01f, 100.0f);

    // Initialize the lighting parameters
    m_lightDir = XMVectorSet(-0.577f, 0.577f, -0.577f, 0.0f);
    m_lightColor = XMVectorSet(0.9f, 0.9f, 0.9f, 1.0f);

    // Initialize the mesh output color
    m_outputColor = XMVectorSet(0, 0, 0, 0);
}

void D3D12Stenciling::OnInit()
{
    LoadPipeline();
    LoadAssets();
}

// Load the rendering pipeline dependencies.
void D3D12Stenciling::LoadPipeline()
{
    UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
    // Enable the debug layer (requires the Graphics Tools "optional feature").
    // NOTE: Enabling the debug layer after device creation will invalidate the active device.
    {
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
        {
            debugController->EnableDebugLayer();

            // Enable additional debug layers.
            dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
    }
#endif

    ComPtr<IDXGIFactory4> factory;
    ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

    if (m_useWarpDevice)
    {
        ComPtr<IDXGIAdapter> warpAdapter;
        ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

        ThrowIfFailed(D3D12CreateDevice(
            warpAdapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&m_device)
        ));
    }
    else
    {
        ComPtr<IDXGIAdapter1> hardwareAdapter;
        GetHardwareAdapter(factory.Get(), &hardwareAdapter);

        ThrowIfFailed(D3D12CreateDevice(
            hardwareAdapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&m_device)
        ));
    }

    // Describe and create the command queue.
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));

    // Describe and create the swap chain.
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = FrameCount;
    swapChainDesc.Width = m_width;
    swapChainDesc.Height = m_height;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;

    ComPtr<IDXGISwapChain1> swapChain;
    ThrowIfFailed(factory->CreateSwapChainForHwnd(
        m_commandQueue.Get(),        // Swap chain needs the queue so that it can force a flush on it.
        Win32Application::GetHwnd(),
        &swapChainDesc,
        nullptr,
        nullptr,
        &swapChain
    ));

    // This sample does not support fullscreen transitions.
    ThrowIfFailed(factory->MakeWindowAssociation(Win32Application::GetHwnd(), DXGI_MWA_NO_ALT_ENTER));

    ThrowIfFailed(swapChain.As(&m_swapChain));
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    // Create descriptor heaps.
    {
        // Describe and create a render target view (RTV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors = FrameCount;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));

        // Describe and create a depth stencil view (DSV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
        dsvHeapDesc.NumDescriptors = 1;
        dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap)));

        m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    }

    // Create frame resources.
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

        // Create a RTV and a command allocator for each frame.
        for (UINT n = 0; n < FrameCount; n++)
        {
            ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
            m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);
            rtvHandle.Offset(1, m_rtvDescriptorSize);

            ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocators[n])));
        }
    }

    // Create the depth-stencil buffer, and the related view.
    {
        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R24G8_TYPELESS, m_width, m_height, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE), // Performance tip: Deny shader resource access to resources that don't need shader resource views.
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &CD3DX12_CLEAR_VALUE(DXGI_FORMAT_D24_UNORM_S8_UINT, 1.0f, 0), // Performance tip: Tell the runtime at resource creation the desired clear value.
            IID_PPV_ARGS(&m_depthStencil)
        ));

        D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilDesc = {};
        depthStencilDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        depthStencilDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        depthStencilDesc.Flags = D3D12_DSV_FLAG_NONE;
        m_device->CreateDepthStencilView(m_depthStencil.Get(), &depthStencilDesc, m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
    }
}

// Load the sample assets.
void D3D12Stenciling::LoadAssets()
{
    D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

    // This is the highest version the sample supports. If CheckFeatureSupport succeeds, the HighestVersion returned will not be greater than this.
    featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

    if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
    {
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
    }

    // Create a root signature with one constant buffer view.
    {
        CD3DX12_ROOT_PARAMETER1 rp[1] = {};
        rp[0].InitAsConstantBufferView(0, 0);

        // Allow input layout and deny uneccessary access to certain pipeline stages.
        D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
        rootSignatureDesc.Init_1_1(_countof(rp), rp, 0, nullptr, rootSignatureFlags);

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;
        ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
        ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
    }

    // Create the constant buffer memory and map the resource
    {
        const D3D12_HEAP_PROPERTIES uploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        size_t cbSize = c_numDrawCalls * FrameCount * sizeof(PaddedConstantBuffer);

        const D3D12_RESOURCE_DESC constantBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(cbSize);
        ThrowIfFailed(m_device->CreateCommittedResource(
            &uploadHeapProperties,
            D3D12_HEAP_FLAG_NONE,
            &constantBufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(m_perFrameConstants.ReleaseAndGetAddressOf())));

        ThrowIfFailed(m_perFrameConstants->Map(0, nullptr, reinterpret_cast<void**>(&m_mappedConstantData)));

        // GPU virtual address of the resource
        m_constantDataGpuAddr = m_perFrameConstants->GetGPUVirtualAddress();
    }

    // Create the pipeline state objects, which includes compiling and loading shaders.
    {
        ComPtr<ID3DBlob> triangleVS;
        ComPtr<ID3DBlob> lambertPS;
        ComPtr<ID3DBlob> solidColorPS;

#if defined(_DEBUG)
        // Enable better shader debugging with the graphics debugging tools.
        UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        UINT compileFlags = 0;
#endif

        ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath(L"shaders.hlsl").c_str(), nullptr, nullptr, "TriangleVS", "vs_5_0", compileFlags, 0, &triangleVS, nullptr));
        ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath(L"shaders.hlsl").c_str(), nullptr, nullptr, "LambertPS", "ps_5_0", compileFlags, 0, &lambertPS, nullptr));
        ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath(L"shaders.hlsl").c_str(), nullptr, nullptr, "SolidColorPS", "ps_5_0", compileFlags, 0, &solidColorPS, nullptr));


        // Define the vertex input layout.
        D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        };

        // Create the Pipeline State Objects
        {
            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};

            //
            // Create the Pipeline State Object for drawing illuminated objects
            //
            psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
            psoDesc.pRootSignature = m_rootSignature.Get();
            psoDesc.VS = CD3DX12_SHADER_BYTECODE(triangleVS.Get());
            psoDesc.PS = CD3DX12_SHADER_BYTECODE(lambertPS.Get());
            psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
            psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
            psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
            psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
            psoDesc.SampleMask = UINT_MAX;
            psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            psoDesc.NumRenderTargets = 1;
            psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
            psoDesc.SampleDesc.Count = 1;
            ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_lambertPipelineState)));

            //
            // Create the Pipeline State Object for drawing objects with a solid color
            //
            psoDesc.PS = CD3DX12_SHADER_BYTECODE(solidColorPS.Get());
            ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_solidColorPipelineState)));

            //
            // Create the Pipeline State Object for drawing transparent objects
            //
            // Use alpha blending
            CD3DX12_BLEND_DESC blendDesc(D3D12_DEFAULT);
            blendDesc.RenderTarget[0].BlendEnable = TRUE;
            blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
            blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
            blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD; // set by the default state, so you can omit it.

            psoDesc.BlendState = blendDesc;
            ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_blendingPipelineState)));

            //
            // PSO for drawing on the stencil buffer (to create a mask)
            //             
            // Disable writes to the render target
            blendDesc.RenderTarget[0].BlendEnable = FALSE;
            blendDesc.RenderTarget[0].RenderTargetWriteMask = 0;

            // Enable depth and stencil tests, while disabling writes to the depth buffer
            CD3DX12_DEPTH_STENCIL_DESC depthdDesc(D3D12_DEFAULT);
            depthdDesc.DepthEnable = TRUE;
            depthdDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
            depthdDesc.StencilEnable = TRUE;
            // A pixel on the front face of a primitive will ALWAYS pass the stencil test, and the value in
            // the corresponding texel of the stencil buffer will be REPLACEed with the stencil reference value
            // if the pixel also passes the depth test.
            depthdDesc.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
            depthdDesc.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
            depthdDesc.FrontFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;
            depthdDesc.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;

            psoDesc.BlendState = blendDesc;
            psoDesc.DepthStencilState = depthdDesc;
            ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_stencilPipelineState)));

            //
            // PSO for drawing reflected, illuminated objects (using the stencil buffer as a mask)
            //
            // Enable writes to the render target
            blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

            // Enable writes to the depth buffer
            depthdDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
            // Enable both depth and stencil tests
            // A pixel on the front face of a primitive will pass the stencil test if the value
            // of the corresponding texel of the stencil buffer is EQUAL to the stencil reference value.
            // The texel KEEPs its value if the pixel also passes the depth test.
            depthdDesc.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
            depthdDesc.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;

            psoDesc.PS = CD3DX12_SHADER_BYTECODE(lambertPS.Get());
            psoDesc.BlendState = blendDesc;
            psoDesc.DepthStencilState = depthdDesc;
            psoDesc.RasterizerState.FrontCounterClockwise = TRUE; // The front is considered the side where the vertices are in counterclockwise order.
            ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_reflectedLambertianPipelineState)));

            //
            // PSO for drawing reflected, NON-illuminated objects (using the stencil buffer as a mask)
            //
            psoDesc.PS = CD3DX12_SHADER_BYTECODE(solidColorPS.Get());
            ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_reflectedSolidColorPipelineState)));

            //
            // PSO for drawing transparent objects projected on other surfaces like shadows.
            //
            // Use alpha blending
            blendDesc.RenderTarget[0].BlendEnable = TRUE;

            // Both depth and stencil tests are used. To prevent double blending:
            // A pixel on the front face of a primitive will pass the stencil test if the value
            // of the corresponding texel of the stencil buffer is EQUAL to the stencil reference value.
            // The texel value is INCRemented if the pixel also passes the depth test.
            depthdDesc.FrontFace.StencilPassOp = D3D12_STENCIL_OP_INCR;

            psoDesc.PS = CD3DX12_SHADER_BYTECODE(solidColorPS.Get());
            psoDesc.BlendState = blendDesc;
            psoDesc.DepthStencilState = depthdDesc;
            psoDesc.RasterizerState.FrontCounterClockwise = FALSE; // <-- irrelevant!
            ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_projectedPipelineState)));
        }
    }

    // Create the command list.
    ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocators[m_frameIndex].Get(), nullptr, IID_PPV_ARGS(&m_commandList)));

    // Command lists are created in the recording state, but there is nothing
    // to record yet. The main loop expects it to be closed, so close it now.
    ThrowIfFailed(m_commandList->Close());

    // Create vertex and index buffers.
    {
        // Define all geometries in a single vertex buffer
        static const Vertex vertices[] =
        {
            // Cube (24 vertices: 0-23)
            { XMFLOAT3(-1.0f, 1.0f, -1.0f), XMFLOAT3(0.0f, 1.0f, 0.0f) },
            { XMFLOAT3(1.0f, 1.0f, -1.0f), XMFLOAT3(0.0f, 1.0f, 0.0f) },
            { XMFLOAT3(1.0f, 1.0f, 1.0f), XMFLOAT3(0.0f, 1.0f, 0.0f) },
            { XMFLOAT3(-1.0f, 1.0f, 1.0f), XMFLOAT3(0.0f, 1.0f, 0.0f) },

            { XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT3(0.0f, -1.0f, 0.0f) },
            { XMFLOAT3(1.0f, -1.0f, -1.0f), XMFLOAT3(0.0f, -1.0f, 0.0f) },
            { XMFLOAT3(1.0f, -1.0f, 1.0f), XMFLOAT3(0.0f, -1.0f, 0.0f) },
            { XMFLOAT3(-1.0f, -1.0f, 1.0f), XMFLOAT3(0.0f, -1.0f, 0.0f) },

            { XMFLOAT3(-1.0f, -1.0f, 1.0f), XMFLOAT3(-1.0f, 0.0f, 0.0f) },
            { XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT3(-1.0f, 0.0f, 0.0f) },
            { XMFLOAT3(-1.0f, 1.0f, -1.0f), XMFLOAT3(-1.0f, 0.0f, 0.0f) },
            { XMFLOAT3(-1.0f, 1.0f, 1.0f), XMFLOAT3(-1.0f, 0.0f, 0.0f) },

            { XMFLOAT3(1.0f, -1.0f, 1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f) },
            { XMFLOAT3(1.0f, -1.0f, -1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f) },
            { XMFLOAT3(1.0f, 1.0f, -1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f) },
            { XMFLOAT3(1.0f, 1.0f, 1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f) },

            { XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT3(0.0f, 0.0f, -1.0f) },
            { XMFLOAT3(1.0f, -1.0f, -1.0f), XMFLOAT3(0.0f, 0.0f, -1.0f) },
            { XMFLOAT3(1.0f, 1.0f, -1.0f), XMFLOAT3(0.0f, 0.0f, -1.0f) },
            { XMFLOAT3(-1.0f, 1.0f, -1.0f), XMFLOAT3(0.0f, 0.0f, -1.0f) },

            { XMFLOAT3(-1.0f, -1.0f, 1.0f), XMFLOAT3(0.0f, 0.0f, 1.0f) },
            { XMFLOAT3(1.0f, -1.0f, 1.0f), XMFLOAT3(0.0f, 0.0f, 1.0f) },
            { XMFLOAT3(1.0f, 1.0f, 1.0f), XMFLOAT3(0.0f, 0.0f, 1.0f) },
            { XMFLOAT3(-1.0f, 1.0f, 1.0f), XMFLOAT3(0.0f, 0.0f, 1.0f) },

            // Floor (4 vertices: 24-27)
            { XMFLOAT3(-3.5f, 0.0f, -10.0f), XMFLOAT3(0.0f, 1.0f, 0.0f) },
            { XMFLOAT3(-3.5f, 0.0f, 0.0f), XMFLOAT3(0.0f, 1.0f, 0.0f) },
            { XMFLOAT3(7.5f, 0.0f, 0.0f), XMFLOAT3(0.0f, 1.0f, 0.0f) },
            { XMFLOAT3(7.5f, 0.0f, -10.0f), XMFLOAT3(0.0f, 1.0f, 0.0f) },

            // Wall (10 vertices: 28-37): we leave a gap in the middle for the mirror
            { XMFLOAT3(-3.5f, 0.0f, 0.0f), XMFLOAT3(0.0f, 0.0f, -1.0f) },
            { XMFLOAT3(-3.5f, 4.0f, 0.0f), XMFLOAT3(0.0f, 0.0f, -1.0f) },
            { XMFLOAT3(-2.5f, 4.0f, 0.0f), XMFLOAT3(0.0f, 0.0f, -1.0f) },
            { XMFLOAT3(-2.5f, 0.0f, 0.0f), XMFLOAT3(0.0f, 0.0f, -1.0f) },

            { XMFLOAT3(2.5f, 0.0f, 0.0f), XMFLOAT3(0.0f, 0.0f, -1.0f) },
            { XMFLOAT3(2.5f, 4.0f, 0.0f), XMFLOAT3(0.0f, 0.0f, -1.0f) },
            { XMFLOAT3(7.5f, 4.0f, 0.0f), XMFLOAT3(0.0f, 0.0f, -1.0f) },
            { XMFLOAT3(7.5f, 0.0f, 0.0f), XMFLOAT3(0.0f, 0.0f, -1.0f) },

            { XMFLOAT3(-3.5f, 6.0f, 0.0f), XMFLOAT3(0.0f, 0.0f, -1.0f) },
            { XMFLOAT3(7.5f, 6.0f, 0.0f), XMFLOAT3(0.0f, 0.0f, -1.0f) },

            // Mirror (4 vertices: 38-41)
            { XMFLOAT3(-2.5f, 0.0f, 0.0f), XMFLOAT3(0.0f, 0.0f, -1.0f) },
            { XMFLOAT3(-2.5f, 4.0f, 0.0f), XMFLOAT3(0.0f, 0.0f, -1.0f) },
            { XMFLOAT3(2.5f, 4.0f, 0.0f), XMFLOAT3(0.0f, 0.0f, -1.0f) },
            { XMFLOAT3(2.5f, 0.0f, 0.0f), XMFLOAT3(0.0f, 0.0f, -1.0f) },
        };

        const UINT vertexBufferSize = sizeof(vertices);

        // Note: using upload heaps to transfer static data like vert buffers is not 
        // recommended. Every time the GPU needs it, the upload heap will be marshalled 
        // over. Please read up on Default Heap usage. An upload heap is used here for 
        // code simplicity and because there are very few verts to actually transfer.
        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_vertexBuffer)));

        // Copy geometry data to the vertex buffer.
        UINT8* pVertexDataBegin = nullptr;
        CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
        ThrowIfFailed(m_vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin)));
        memcpy(pVertexDataBegin, vertices, sizeof(vertices));
        m_vertexBuffer->Unmap(0, nullptr);

        // Initialize the vertex buffer view.
        m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
        m_vertexBufferView.StrideInBytes = sizeof(Vertex);
        m_vertexBufferView.SizeInBytes = vertexBufferSize;

        // Create index buffer
        static const uint16_t indices[] =
        {
            //
            // Cube (36 incides: 0-35)
            //  
            // TOP
            3,1,0,
            2,1,3,

            // BOTTOM
            6,4,5,
            7,4,6,

            // RIGHT
            11,9,8,
            10,9,11,

            // LEFT
            14,12,13,
            15,12,14,

            // FRONT
            19,17,16,
            18,17,19,

            // BACK
            22,20,21,
            23,20,22,

            // Floor (6 indices: 36-41)
            0, 1, 2,
            0, 2, 3,

            // Wall (18 indices: 42-59)
            0, 1, 2,
            0, 2, 3,

            4, 5, 6,
            4, 6, 7,

            1, 8, 9,
            1, 9, 6,

            // Mirror (6 indices: 60-65)
            0, 1, 2,
            0, 2, 3
        };

        const UINT indexBufferSize = sizeof(indices);

        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(indexBufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_indexBuffer)));

        // Copy the geometry data to the index buffer.
        ThrowIfFailed(m_indexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin)));
        memcpy(pVertexDataBegin, indices, sizeof(indices));
        m_indexBuffer->Unmap(0, nullptr);

        // Initialize the vertex buffer view.
        m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
        m_indexBufferView.Format = DXGI_FORMAT_R16_UINT;
        m_indexBufferView.SizeInBytes = indexBufferSize;
    }

    // Create synchronization objects and wait until assets have been uploaded to the GPU.
    {
        ThrowIfFailed(m_device->CreateFence(m_fenceValues[m_frameIndex], D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
        m_fenceValues[m_frameIndex]++;

        // Create an event handle to use for frame synchronization.
        m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (m_fenceEvent == nullptr)
        {
            ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
        }

        // Wait for the command list to execute; we are reusing the same command 
        // list in our main loop but for now, we just want to wait for setup to 
        // complete before continuing.
        WaitForGpu();
    }
}

// Update frame-based values.
void D3D12Stenciling::OnUpdate()
{
    const float rotationSpeed = 0.015f;

    // Update the rotation constant
    m_curRotationAngleRad += rotationSpeed;
    if (m_curRotationAngleRad >= XM_2PI)
    {
        m_curRotationAngleRad -= XM_2PI;
    }

    // Rotate the cube around the Y-axis, and translate it over the floor, and in front of the wall
    m_cubeWorldMatrix = XMMatrixRotationY(m_curRotationAngleRad) * XMMatrixTranslation(0.0f, 2.0f, -6.0f);
}

// Render the scene.
void D3D12Stenciling::OnRender()
{
    // Record all the commands we need to render the scene into the command list.
    PopulateCommandList();

    // Execute the command list.
    ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    // Present the frame.
    ThrowIfFailed(m_swapChain->Present(1, 0));

    MoveToNextFrame();
}

void D3D12Stenciling::OnDestroy()
{
    // Ensure that the GPU is no longer referencing resources that are about to be
    // cleaned up by the destructor.
    WaitForGpu();

    CloseHandle(m_fenceEvent);
}

void D3D12Stenciling::PopulateCommandList()
{
    // Command list allocators can only be reset when the associated 
    // command lists have finished execution on the GPU; apps should use 
    // fences to determine GPU execution progress.
    ThrowIfFailed(m_commandAllocators[m_frameIndex]->Reset());

    // However, when ExecuteCommandList() is called on a particular command 
    // list, that command list can then be reset at any time and must be before 
    // re-recording.
    ThrowIfFailed(m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), m_lambertPipelineState.Get()));

    // Set necessary state.
    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
    m_commandList->RSSetViewports(1, &m_viewport);
    m_commandList->RSSetScissorRects(1, &m_scissorRect);

    // Index into the available constant buffers based on the number
    // of draw calls. We've allocated enough for a known number of
    // draw calls per frame times the number of back buffers
    unsigned int constantBufferIndex = c_numDrawCalls * (m_frameIndex % FrameCount);

    // Set the per-frame constants
    ConstantBuffer cbParameters = {};

    // Shaders compiled with default row-major matrices
    XMStoreFloat4x4(&cbParameters.worldMatrix, XMMatrixTranspose(m_cubeWorldMatrix));
    XMStoreFloat4x4(&cbParameters.viewMatrix, XMMatrixTranspose(m_viewMatrix));
    XMStoreFloat4x4(&cbParameters.projectionMatrix, XMMatrixTranspose(m_projectionMatrix));

    XMStoreFloat4(&cbParameters.lightDir, m_lightDir);
    XMStoreFloat4(&cbParameters.lightColor, m_lightColor);
    XMStoreFloat4(&cbParameters.outputColor, m_outputColor);

    // Set the constants for the first draw call
    memcpy(&m_mappedConstantData[constantBufferIndex], &cbParameters, sizeof(ConstantBuffer));

    // Bind the constants to the shader
    auto baseGpuAddress = m_constantDataGpuAddr + sizeof(PaddedConstantBuffer) * constantBufferIndex;
    m_commandList->SetGraphicsRootConstantBufferView(0, baseGpuAddress);

    // Indicate that the back buffer will be used as a render target.
    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

    // Set render target and depth buffer in OM stage
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
    CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
    m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

    // Clear the render target and depth buffer
    const float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    m_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    // Set up the input assembler
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
    m_commandList->IASetIndexBuffer(&m_indexBufferView);

    // Draw the Lambert lit cube
    m_commandList->DrawIndexedInstanced(36, 1, 0, 0, 0);
    baseGpuAddress += sizeof(PaddedConstantBuffer);
    ++constantBufferIndex;

    // Set PSO for opaque objects
    m_commandList->SetPipelineState(m_solidColorPipelineState.Get());

    // Draw Floor and Wall
    for (int m = 0; m < 2; ++m)
    {
        // Update world matrix and output color
        XMStoreFloat4x4(&cbParameters.worldMatrix, XMMatrixIdentity());
        cbParameters.outputColor = m ? XMFLOAT4(0.6f, 0.3f, 0.0f, 1.0f) : XMFLOAT4(1.0f, 0.9f, 0.7f, 1.0f);

        // Set the constants for the draw call
        memcpy(&m_mappedConstantData[constantBufferIndex], &cbParameters, sizeof(ConstantBuffer));

        // Bind the constants to the shader
        m_commandList->SetGraphicsRootConstantBufferView(0, baseGpuAddress);

        if (m)
            m_commandList->DrawIndexedInstanced(18, 1, 42, 28, 0); // Wall
        else
            m_commandList->DrawIndexedInstanced(6, 1, 36, 24, 0);  // Floor

        baseGpuAddress += sizeof(PaddedConstantBuffer);
        ++constantBufferIndex;
    }

    // Set PSO for drawing on the stencil buffer
    m_commandList->SetPipelineState(m_stencilPipelineState.Get());

    // Set the stencil ref. value to 1
    m_commandList->OMSetStencilRef(1);

    // Draw on the stencil buffer to mark the mirror
    // We can re-use the constant buffer already bound to the pipeline
    m_commandList->DrawIndexedInstanced(6, 1, 60, 38, 0);
    baseGpuAddress += sizeof(PaddedConstantBuffer);
    ++constantBufferIndex;

    // Set PSO for reflected, lit objects (the cube)
    m_commandList->SetPipelineState(m_reflectedLambertianPipelineState.Get());

    // Update the world matrix of the cube to reflect it with respect to the mirror
    XMVECTOR mirrorPlane = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f); // xy-plane
    XMMATRIX R = XMMatrixReflect(mirrorPlane);
    XMStoreFloat4x4(&cbParameters.worldMatrix, XMMatrixTranspose(m_cubeWorldMatrix * R));

    // Set the constants for the draw call
    memcpy(&m_mappedConstantData[constantBufferIndex], &cbParameters, sizeof(ConstantBuffer));

    // Bind the constants to the shader
    m_commandList->SetGraphicsRootConstantBufferView(0, baseGpuAddress);

    // Draw the reflected, lit cube
    m_commandList->DrawIndexedInstanced(36, 1, 0, 0, 0);
    baseGpuAddress += sizeof(PaddedConstantBuffer);
    ++constantBufferIndex;

    // Set PSO for reflected, non-illuminated objects (floor and shadow)
    m_commandList->SetPipelineState(m_reflectedSolidColorPipelineState.Get());

    // Update world matrix and output color of the floor to reflect it with respect to the mirror
    XMStoreFloat4x4(&cbParameters.worldMatrix, XMMatrixTranspose(XMMatrixIdentity() * R));
    cbParameters.outputColor = XMFLOAT4(1.0f, 0.9f, 0.7f, 1.0f);

    // Set the constants for the draw call
    memcpy(&m_mappedConstantData[constantBufferIndex], &cbParameters, sizeof(ConstantBuffer));

    // Bind the constants to the shader
    m_commandList->SetGraphicsRootConstantBufferView(0, baseGpuAddress);

    // Draw the reflected floor
    m_commandList->DrawIndexedInstanced(6, 1, 36, 24, 0);
    baseGpuAddress += sizeof(PaddedConstantBuffer);
    ++constantBufferIndex;

    // Set PSO for transparent object projected on other surfaces (planar shadow of the cube)
    m_commandList->SetPipelineState(m_projectedPipelineState.Get());

    // Reset stencil ref. value to 0
    m_commandList->OMSetStencilRef(0);

    // Update world matrix and output color to project the cube onto the floor with respect to 
    // the light source, and raise it a little to prevent z-fighting.
    XMVECTOR shadowPlane = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f); // xz-plane
    XMMATRIX S = XMMatrixShadow(shadowPlane, m_lightDir);
    XMMATRIX shadowOffsetY = XMMatrixTranslation(0.0f, 0.003f, 0.0f);
    XMStoreFloat4x4(&cbParameters.worldMatrix, XMMatrixTranspose(m_cubeWorldMatrix * S * shadowOffsetY));
    cbParameters.outputColor = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.2f);

    // Set the constants for the draw call
    memcpy(&m_mappedConstantData[constantBufferIndex], &cbParameters, sizeof(ConstantBuffer));

    // Bind the constants to the shader
    m_commandList->SetGraphicsRootConstantBufferView(0, baseGpuAddress);

    // Draw the shadow of the cube
    m_commandList->DrawIndexedInstanced(36, 1, 0, 0, 0);
    baseGpuAddress += sizeof(PaddedConstantBuffer);
    ++constantBufferIndex;

    // Set stencil ref. value to 1
    m_commandList->OMSetStencilRef(1);

    // Update world matrix and output color to draw the shadow of the cube
    // reflected into the mirror.
    XMStoreFloat4x4(&cbParameters.worldMatrix, XMMatrixTranspose(m_cubeWorldMatrix * S * shadowOffsetY * R));
    cbParameters.outputColor = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.2f);

    // Set the constants for the draw call
    memcpy(&m_mappedConstantData[constantBufferIndex], &cbParameters, sizeof(ConstantBuffer));

    // Bind the constants to the shader
    m_commandList->SetGraphicsRootConstantBufferView(0, baseGpuAddress);

    // Draw the shadow of the cube reflected into the mirror
    m_commandList->DrawIndexedInstanced(36, 1, 0, 0, 0);
    baseGpuAddress += sizeof(PaddedConstantBuffer);
    ++constantBufferIndex;

    // Set PSO for transparent objects (mirror)
    m_commandList->SetPipelineState(m_blendingPipelineState.Get());

    // Update world matrix and output color.
    XMStoreFloat4x4(&cbParameters.worldMatrix, XMMatrixIdentity());
    cbParameters.outputColor = XMFLOAT4(0.5f, 1.0f, 1.0f, 0.15f);

    // Set the constants for the draw call
    memcpy(&m_mappedConstantData[constantBufferIndex], &cbParameters, sizeof(ConstantBuffer));

    // Bind the constants to the shader
    m_commandList->SetGraphicsRootConstantBufferView(0, baseGpuAddress);

    // Draw the mirror
    m_commandList->DrawIndexedInstanced(6, 1, 60, 38, 0);

    // Indicate that the back buffer will now be used to present.
    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    ThrowIfFailed(m_commandList->Close());
}

// Wait for pending GPU work to complete.
void D3D12Stenciling::WaitForGpu()
{
    // Schedule a Signal command in the queue.
    ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), m_fenceValues[m_frameIndex]));

    // Wait until the fence has been processed.
    ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent));
    WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);

    // Increment the fence value for the current frame.
    m_fenceValues[m_frameIndex]++;
}

// Prepare to render the next frame.
void D3D12Stenciling::MoveToNextFrame()
{
    // Schedule a Signal command in the queue.
    const UINT64 currentFenceValue = m_fenceValues[m_frameIndex];
    ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), currentFenceValue));

    // Update the frame index.
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    // If the next frame is not ready to be rendered yet, wait until it is ready.
    if (m_fence->GetCompletedValue() < m_fenceValues[m_frameIndex])
    {
        ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent));
        WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
    }

    // Set the fence value for the next frame.
    m_fenceValues[m_frameIndex] = currentFenceValue + 1;
}