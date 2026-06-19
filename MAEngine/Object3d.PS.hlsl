#include "object3d.hlsli"

struct Material
{
    float32_t4 color;
    int32_t enableLighting; // スライドに合わせて追加
};
ConstantBuffer<Material> gMaterial : register(b0);

struct DirectionalLight
{
    float4 color;
    float3 direction;
    float intensity;
};
ConstantBuffer<DirectionalLight> gDirectionalLight : register(b1);

// テクスチャとサンプラーの受け口
Texture2D<float32_t4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct PixelShaderOutput
{
    float32_t4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    
    // テクスチャのサンプル
    float32_t4 textureColor = gTexture.Sample(gSampler, input.texcoord);
    
    // ライティングの計算
    if (gMaterial.enableLighting != 0)
    { // Lightingする場合
        // 1. 法線の正規化と内積計算（光の方向は反転させる）
        float cos = saturate(dot(normalize(input.normal), -gDirectionalLight.direction));
        
        // 2. ランバート反射モデルの適用
        output.color = gMaterial.color * textureColor * gDirectionalLight.color * cos * gDirectionalLight.intensity;
    }
    else
    { // Lightingしない場合
        output.color = gMaterial.color * textureColor;
    }
    
    return output;
}