#pragma once
#include "Matrix4x4.h"
#include "Vector3.h"
#include <Windows.h>

class DebugCamera
{
public:
    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize();

    /// <summary>
    /// 更新 (WinMainからキーボードの入力配列を受け取る)
    /// </summary>
    void Update(const BYTE* keys);

private:
    Matrix4x4 matRot_;

    // ローカル座標 (初期位置は奥に50下げた状態)
    Vector3 translation_ = { 0.0f, 0.0f, -50.0f };

    // ビュー行列
    Matrix4x4 viewMatrix_;

    // 射影行列
    Matrix4x4 projectionMatrix_;
};