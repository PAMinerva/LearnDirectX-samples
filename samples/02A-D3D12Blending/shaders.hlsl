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

//--------------------------------------------------------------------------------------
// Constant Buffer Variables
//--------------------------------------------------------------------------------------
cbuffer ConstantBuffer : register(b0)
{
	matrix World;
	matrix View;
	matrix Projection;
	float4 outputColor;
}

//--------------------------------------------------------------------------------------
struct PSInput
{
	float4 position : SV_POSITION;
	float4 color : COLOR;
};


//--------------------------------------------------------------------------------------
// Vertex Shader
//--------------------------------------------------------------------------------------
PSInput VSMain(float4 position : POSITION, float4 color : COLOR)
{
	PSInput output = (PSInput) 0;
	output.position = mul(position, World);
	output.position = mul(output.position, View);
	output.position = mul(output.position, Projection);
	output.color = color;
    
	return output;
}


//--------------------------------------------------------------------------------------
// Name: PSMain
// Desc: Default pixel shader returning an interpolated color
//--------------------------------------------------------------------------------------
float4 PSMain(PSInput input) : SV_TARGET
{
	return input.color;
}

//--------------------------------------------------------------------------------------
// Name: SolidColorPS
// Desc: Pixel shader applying solid color
//--------------------------------------------------------------------------------------
float4 SolidColorPS(PSInput input) : SV_Target
{
	return outputColor;
}