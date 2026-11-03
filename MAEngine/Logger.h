#pragma once
#include <string>

// ログ出力クラス
// Visual Studio の出力ウィンドウと logs/ フォルダ内のファイルの両方に書き出す
class Logger {
public:
	// logs フォルダを作り、日時付きのログファイルを開く
	static void Initialize();

	// ログファイルを閉じる
	static void Finalize();

	// ログ出力（出力ウィンドウ + ファイル）
	static void Log(const std::string& message);
	static void Log(const std::wstring& message);

	// "20261103_162912" 形式の日時文字列（ファイル名用）
	static std::string GetTimeStringForFile();

private:
	Logger() = delete;
};
