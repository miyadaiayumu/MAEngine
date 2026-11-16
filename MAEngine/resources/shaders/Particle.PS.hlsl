#include "Particle.hlsli"

struct Material
{
    float32_t4 color;
    int32_t enableLighting;
    int32_t lightingType;
    float padding;
    float32_t4x4 uvTransform;
};
ConstantBuffer<Material> gMaterial : register(b0);

// テクスチャとサンプラー
Texture2D<float32_t4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct PixelShaderOutput
{
    float32_t4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    
    // UV変換
    float32_t4 transformedUV = mul(float32_t4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float32_t4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);
    
    // 色の計算
    output.color = gMaterial.color * textureColor;
    
    // 最終的なαが0のときだけdiscard
    if (output.color.a == 0.0f)
    {
        discard;
    }
    
    return output;
}