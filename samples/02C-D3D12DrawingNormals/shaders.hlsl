//--------------------------------------------------------------------------------------
// Shader demonstrating Lambertian lighting from multiple sources
//
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
	float4 lightDir;
	float4 lightColor;
	float4 outputColor;
};

 
//--------------------------------------------------------------------------------------
struct VS_INPUT
{
	float4 Pos : POSITION;
	float3 Normal : NORMAL;
};

struct GS_INPUT
{
	float4 Pos : POSITION;
};

struct GS_OUTPUT
{
	float4 Pos : SV_POSITION;
};

struct PS_INPUT
{
	float4 Pos : SV_POSITION;
	float3 Normal : NORMAL;
};


//--------------------------------------------------------------------------------------
// Name: mainVS
// Desc: Vertex shader
//--------------------------------------------------------------------------------------
PS_INPUT MainVS(VS_INPUT input)
{
	PS_INPUT output = (PS_INPUT)0;
	output.Pos = mul(input.Pos, mWorld);
	output.Pos = mul(output.Pos, mView);
	output.Pos = mul(output.Pos, mProjection);
	output.Normal = mul(input.Normal, ((float3x3) mWorld));
    
	return output;
}


//--------------------------------------------------------------------------------------
// Name: PassThroughVS
// Desc: VS that simply passes the position to the GS
//--------------------------------------------------------------------------------------
GS_INPUT PassThroughVS(VS_INPUT In)
{
	GS_INPUT Out;
	Out.Pos = In.Pos;
	return Out;
}


//--------------------------------------------------------------------------------------
// Name: MainGS
// Desc: Geometry shader for drawing normals to triangles
//--------------------------------------------------------------------------------------
[maxvertexcount(2)]
void MainGS(triangle GS_INPUT In[3], inout LineStream<GS_OUTPUT> stream)
{
	// Get the local positions of the vertices of the input triangle
    float4 v0 = In[0].Pos;
	float4 v1 = In[1].Pos;
	float4 v2 = In[2].Pos;
    
    // Sides of the input triangle
    float3 e1 = normalize(v1 - v0).xyz;
    float3 e2 = normalize(v2 - v0).xyz;
    
    // The normal is the cross product of the sides.
	// v0, v1 and v2 in clockwise order so it's e1 x e2
    float3 normal = normalize(cross(e1, e2));
    
	// Show the normal at the center of the triangle
    float4 center = (v0 + v1 + v2) / 3.0;
	
	// We need to transform the line segment (two points) representing
	// the normal from local to clip space.
	float4x4 mVP = mul(mWorld, mul(mView, mProjection));
    
	// Emit the two points of a line segment representing the normal to the input triangle
    for (int i = 0; i < 2; ++i)
    {
        GS_OUTPUT v;
        center.xyz += normal * 0.3 * i;
        v.Pos = mul(center, mVP);
		stream.Append(v);
	}
}


//--------------------------------------------------------------------------------------
// Name: LambertPS
// Desc: Pixel shader applying Lambertian lighting
//--------------------------------------------------------------------------------------
float4 LambertPS(PS_INPUT input) : SV_Target
{
	float4 finalColor = 0;
    
    //do NdotL lighting for one lights
	finalColor += saturate(dot((float3) lightDir, input.Normal) * lightColor);
	
	finalColor.a = 1;
	return finalColor;
}


//--------------------------------------------------------------------------------------
// Name: SolidColorPS
// Desc: Pixel shader applying solid color
//--------------------------------------------------------------------------------------
float4 SolidColorPS(GS_OUTPUT input) : SV_Target
{
	return outputColor;
}