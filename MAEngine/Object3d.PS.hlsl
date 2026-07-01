#include "object3d.hlsli"

struct Material
{
    float32_t4 color;
    int32_t enableLighting; // スライドに合わせて追加
    float32_t4x4 uvTransform; // UV変換用行列を追加
};

ConstantBuffer<Material> gMaterial : register(b0);

struct DirectionalLight
{
    float32_t4 color; // 型をfloat32_t系に統一
    float32_t3 direction;
    float32_t intensity;
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
    
    // --- UV変換の追加 ---
    float4 transformedUV = mul(float32_t4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    
    // 変換後のxyをテクスチャサンプリングに使用
    float32_t4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);
    
    // ライティングの計算
    if (gMaterial.enableLighting != 0)
    {
        // 1. 法線の正規化と内積計算（光の方向は反転させる）
        float32_t NdotL = dot(normalize(input.normal), -gDirectionalLight.direction);
        
        // 2. ハーフランバート反射モデルの適用
        float32_t halfLambert = NdotL * 0.5f + 0.5f;
        halfLambert = halfLambert * halfLambert;
        
        // 3. 色の計算（RGBのみにライティングを適用し、アルファ値が影で透けるのを防ぐ）
        float32_t3 diffuse = gMaterial.color.rgb * textureColor.rgb * gDirectionalLight.color.rgb * halfLambert * gDirectionalLight.intensity;
        
        // 最終的な色とアルファを結合
        output.color = float32_t4(diffuse, gMaterial.color.a * textureColor.a);
    }
    else
    { // Lightingしない場合
        output.color = gMaterial.color * textureColor;
    }
    
    return output;
}