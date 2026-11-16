#include "object3d.hlsli"

struct Material
{
    float32_t4 color;
    int32_t enableLighting; // 互換性のために残すか、なくてもOKですが今回は構造を合わせるためそのままにします
    int32_t lightingType; // 0: 無し, 1: ランバート, 2: ハーフランバート
    float padding;
    float32_t4x4 uvTransform;
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
    
    // アルファ値が一定以下（透明領域）のピクセルを破棄する
    if (textureColor.a < 0.5f)
    {
        discard;
    }
    
    // ライティングの計算
    // lightingType が 0 以外、または従来の enableLighting != 0 の場合にライティングを行う
    if (gMaterial.lightingType != 0)
    {
        // 1. 法線の正規化と内積計算（光の方向は反転させる）
        float32_t NdotL = dot(normalize(input.normal), -gDirectionalLight.direction);
        
        float diffuseFactor = 0.0f;
        
        // 2. 反射モデルの切り替え
        if (gMaterial.lightingType == 1)
        {
            // ランバート反射
            diffuseFactor = saturate(NdotL);
        }
        else if (gMaterial.lightingType == 2)
        {
            // ハーフランバート反射
            float32_t halfLambert = NdotL * 0.5f + 0.5f;
            diffuseFactor = halfLambert * halfLambert;
        }
        
        // 3. 色の計算
        float32_t3 diffuse = gMaterial.color.rgb * textureColor.rgb * gDirectionalLight.color.rgb * diffuseFactor * gDirectionalLight.intensity;
        
        output.color = float32_t4(diffuse, gMaterial.color.a * textureColor.a);
    }
    else
    { // lightingType == 0 (ライティングしない場合)
        output.color = gMaterial.color * textureColor;
    }
    
    return output;
}