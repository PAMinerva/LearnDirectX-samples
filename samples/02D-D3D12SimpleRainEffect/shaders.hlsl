//--------------------------------------------------------------------------------------
// Advanced Technology Group (ATG)
// Copyright (C) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------


//--------------------------------------------------------------------------------------
// Constant Buffer Variables
//--------------------------------------------------------------------------------------
cbuffer Constants : register(b0)
{
	float4x4 mWorld;
	float4x4 mView;
	float4x4 mProjection;
	float4 outputColor;
	float3 cameraWPos;
	float deltaTime;
};

 
//--------------------------------------------------------------------------------------
struct VS_INPUT
{
	float3 Pos : POSITION;
	float2 Size : SIZE;
	float Speed : SPEED;
};

struct GS_OUTPUT
{
	float4 Pos : SV_POSITION;
};


//--------------------------------------------------------------------------------------
// Name: MainVS
// Desc: Pass-through Vertex shader
//--------------------------------------------------------------------------------------
VS_INPUT MainVS(VS_INPUT In)
{
	return In;
}


//--------------------------------------------------------------------------------------
// Name: MainGSSO
// Desc: Geometry shader for moving particles
//--------------------------------------------------------------------------------------
[maxvertexcount(4)]
void MainGSSO(point VS_INPUT input[1], inout PointStream<VS_INPUT> output)
{
	VS_INPUT particle = input[0];
    
    // Decrease the height of the point\particle over time based on its speed
	particle.Pos.y -= particle.Speed * deltaTime;
    
    // Reset the height of the point\particle
	if (particle.Pos.y < -50.0f)
	{
		particle.Pos.y = 50.0f;
	}
	
	// Emit the point\particle with the updated position
	output.Append(particle);
}


//--------------------------------------------------------------------------------------
// Name: MainGS
// Desc: Geometry shader for drawing quads from points\particles
//--------------------------------------------------------------------------------------
[maxvertexcount(4)]
void MainGS(point VS_INPUT input[1], inout TriangleStream<GS_OUTPUT> outputStream)
{
    // World coordinates of the input point\particle
	float3 positionW = mul(float4(input[0].Pos, 1.0f), mWorld).xyz;
    
	// We need the up direction of the world space, and right direction with respect to the camera.
	// We can use the projection of the lookAt vector onto the xz-plane to calculate the right direction.
	float3 up = float3(0.0f, 1.0f, 0.0f);
	float3 look = positionW - cameraWPos;
	look.y = 0.0f;
	look = normalize(look);
	float3 right = cross(up, look);
    
	// Half-size of the input point\particle
	float hw = 0.5f * input[0].Size.x;
	float hh = 0.5f * input[0].Size.y;
    
	// Compute the world coordinates of the quad from the point\particle attributes
	float4 position[4] =
	{
		float4(positionW - (hw * right) - (hh * up), 1.0f), // Left-Bottom
        float4(positionW - (hw * right) + (hh * up), 1.0f), // Left-Top
        float4(positionW + (hw * right) - (hh * up), 1.0f), // Right-Bottom
        float4(positionW + (hw * right) + (hh * up), 1.0f)  // Rright-Top
	};

	// Transform the four vertices of the quad from world to clip space, and
	// emit them as a triangle strip.
	GS_OUTPUT output = (GS_OUTPUT)0;
    [unroll]
	for (int i = 0; i < 4; ++i)
	{
		output.Pos = mul(position[i], mView);
		output.Pos = mul(output.Pos, mProjection);
		outputStream.Append(output);
	}
}


//--------------------------------------------------------------------------------------
// Name: SolidColorPS
// Desc: Pixel shader applying solid color
//--------------------------------------------------------------------------------------
float4 MainPS(GS_OUTPUT input) : SV_Target
{
	return outputColor;
}