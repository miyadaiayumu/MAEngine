#pragma once
#include<windows.h>
#include<wrl.h>

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

class Input {
public:
	template<class T>using ComPtr = Microsoft::WRL::ComPtr<T>;

public:
    void Initialize(HINSTANCE hInstance, HWND hwnd);
    void Update();

	bool PushKey(BYTE keyNumber);

	bool TriggerKey(BYTE keyNumber);

private:
	Microsoft::WRL::ComPtr<IDirectInputDevice8> keyboard;
	Microsoft::WRL::ComPtr<IDirectInput8> directInput;

	BYTE key[256] = {};
	BYTE keyPre[256] = {};
};