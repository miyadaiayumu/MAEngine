#include "Input.h"
#include <cassert>

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")

//using namespace Microsoft::WRL;

void Input::Initialize(HINSTANCE hInstance, HWND hwnd)
{

	HRESULT hr;

    // 1. DirectInputオブジェクトの生成
    Microsoft::WRL::ComPtr<IDirectInput8> directInput;
    hr = DirectInput8Create(
        hInstance,
        DIRECTINPUT_VERSION,
        IID_IDirectInput8,
        reinterpret_cast<void**>(directInput.GetAddressOf()),
        nullptr
    );

    assert(SUCCEEDED(hr));

    // 2. キーボードデバイスの生成
    //Microsoft::WRL::ComPtr<IDirectInputDevice8> keyboard;
    hr = directInput->CreateDevice(GUID_SysKeyboard, keyboard.GetAddressOf(), nullptr);
    assert(SUCCEEDED(hr));

    // 3. 入力データ形式のセット
    hr = keyboard->SetDataFormat(&c_dfDIKeyboard);
    assert(SUCCEEDED(hr));

    // 4. 排他制御レベルのセット
    hr = keyboard->SetCooperativeLevel(
        hwnd,
        DISCL_FOREGROUND | DISCL_NONEXCLUSIVE | DISCL_NOWINKEY
    );
    assert(SUCCEEDED(hr));
}

void Input::Update()
{

	// 前フレームのキー状態を保存
	memcpy(keyPre, key, sizeof(key));

    // 入力更新処理
    keyboard->Acquire();
    

    keyboard->GetDeviceState(sizeof(key), key);

}

bool Input::PushKey(BYTE keyNumber)
{
    if (key[keyNumber]) {
		return true;
    }

    return false;
}

bool Input::TriggerKey(BYTE keyNumber)
{
    if (key[keyNumber] && !keyPre[keyNumber]) {
        return true;
    }
    return false;
}