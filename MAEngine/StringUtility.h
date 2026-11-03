#pragma once
#include <string>

// 文字列変換などの汎用ユーティリティ
namespace StringUtility {

	// std::string (UTF-8) -> std::wstring
	std::wstring ConvertString(const std::string& str);

	// std::wstring -> std::string (UTF-8)
	std::string ConvertString(const std::wstring& str);

}
