#include "DebugCamera.h"
#include "Matrix4x4.h"
#include <dinput.h> // DIK_W 等の定数を使用するため
#include <cstdint>

// ベクトル変換関数
Vector3 Transform(const Vector3& vector, const Matrix4x4& matrix) {
    Vector3 result;
    // ベクトルに回転行列を適用 (w = 0 として方向ベクトルとして扱う)
    result.x = vector.x * matrix.m[0][0] + vector.y * matrix.m[1][0] + vector.z * matrix.m[2][0];
    result.y = vector.x * matrix.m[0][1] + vector.y * matrix.m[1][1] + vector.z * matrix.m[2][1];
    result.z = vector.x * matrix.m[0][2] + vector.y * matrix.m[1][2] + vector.z * matrix.m[2][2];
    return result;
}

// WinMain側に定義されている判定用のヘルパー関数
bool IsPressKey(const BYTE* keys, uint8_t keyNumber) {
    return (keys[keyNumber] & 0x80) != 0;
}

void DebugCamera::Initialize()
{
    // 【修正】MakeIdentityMatrix() から MakeIdentity4x4() に変更
    matRot_ = Matrix4x4::MakeIdentity4x4();
}

void DebugCamera::Update(const BYTE* keys)
{
    // ==========================================
    // 1. 入力によるカメラの移動や回転
    // ==========================================

    // カメラの移動速度と回転速度の定義
    const float moveSpeed = 0.5f;
    const float rotateSpeed = 0.02f;

    // 1フレームの総移動量を蓄積するベクトル
    Vector3 totalMove = { 0.0f, 0.0f, 0.0f };

    // --- 【移動入力】 ---
    // 前後移動 (W / S キー)
    if (IsPressKey(keys, DIK_W)) {
        totalMove.z += moveSpeed; // 前進
    }
    if (IsPressKey(keys, DIK_S)) {
        totalMove.z -= moveSpeed; // 後退
    }

    // 左右移動 (D / A キー)
    if (IsPressKey(keys, DIK_D)) {
        totalMove.x += moveSpeed; // 右移動
    }
    if (IsPressKey(keys, DIK_A)) {
        totalMove.x -= moveSpeed; // 左移動
    }

    // 上下移動 (Space / LSHIFT キー)
    if (IsPressKey(keys, DIK_SPACE)) {
        totalMove.y += moveSpeed; // 上昇
    }
    if (IsPressKey(keys, DIK_LSHIFT)) {
        totalMove.y -= moveSpeed; // 下降
    }


    // --- 【回転入力（ピボット回転の計算）】 ---
    float deltaX = 0.0f; // 今回フレームでのX軸追加回転角度
    float deltaY = 0.0f; // 今回フレームでのY軸追加回転角度

    // X軸周りの回転（上下のピッチ：矢印キー 上 / 下）
    if (IsPressKey(keys, DIK_UP)) {
        deltaX += rotateSpeed;
    }
    if (IsPressKey(keys, DIK_DOWN)) {
        deltaX -= rotateSpeed;
    }

    // Y軸周りの回転（左右のヨー：矢印キー 右 / 左）
    if (IsPressKey(keys, DIK_RIGHT)) {
        deltaY += rotateSpeed;
    }
    if (IsPressKey(keys, DIK_LEFT)) {
        deltaY -= rotateSpeed;
    }

    // 【修正】追加回転分の回転行列を生成 (MakeIdentity4x4 に変更)
    Matrix4x4 matRotDelta = Matrix4x4::MakeIdentity4x4();

    // X軸回転とY軸回転を合成
    Matrix4x4 rotateX = Matrix4x4::MakeRotateXMatrix(deltaX);
    Matrix4x4 rotateY = Matrix4x4::MakeRotateYMatrix(deltaY);
    matRotDelta = Matrix4x4::Multiply(rotateX, rotateY);

    // 【累積の回転行列を合成】
    matRot_ = Matrix4x4::Multiply(matRotDelta, matRot_);


    // --- 【移動ベクトルの回転と座標への加算】 ---
    // 更新した累積回転行列「matRot_」を使用して移動ベクトルを回転させます
    Vector3 rotatedMove = Transform(totalMove, matRot_);

    // カメラの座標に加算する
    translation_.x += rotatedMove.x;
    translation_.y += rotatedMove.y;
    translation_.z += rotatedMove.z;


    // ==========================================
    // 2. ビュー行列の更新
    // ==========================================

    // ① 座標から平行移動行列を計算する
    Matrix4x4 translateMatrix = Matrix4x4::MakeTranslateMatrix(translation_);

    // ② 累積回転行列(matRot_)と平行移動行列からカメラのワールド行列を計算する (R * T)
    Matrix4x4 cameraWorldMatrix = Matrix4x4::Multiply(matRot_, translateMatrix);

    // ③ ワールド行列の逆行列をビュー行列に代入する
    viewMatrix_ = Matrix4x4::Inverse(cameraWorldMatrix);
}