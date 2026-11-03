#include "Logger.h"
#include "StringUtility.h"
#include <Windows.h>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace {
	std::ofstream g_logFile;
}

void Logger::Initialize() {
	if (g_logFile.is_open()) {
		return;
	}

	// logsフォルダ作成
	std::filesystem::create_directories("logs");

	// ファイル名生成
	std::filesystem::path logFilePath =
		std::filesystem::path("logs") / (GetTimeStringForFile() + ".log");

	// ファイルを開きっぱなしにする
	g_logFile.open(logFilePath, std::ios::app);
}

void Logger::Finalize() {
	if (g_logFile.is_open()) {
		g_logFile.close();
	}
}

void Logger::Log(const std::string& message) {
	// デバッグ出力
	OutputDebugStringA((message + "\n").c_str());

	// ファイル出力（クラッシュ直前のログも残るようにその都度flushする）
	if (g_logFile.is_open()) {
		g_logFile << message << std::endl;
	}
}

void Logger::Log(const std::wstring& message) {
	Log(StringUtility::ConvertString(message));
}

std::string Logger::GetTimeStringForFile() {
	auto now = std::chrono::system_clock::now();
	std::time_t t = std::chrono::system_clock::to_time_t(now);

	std::tm tm{};
	localtime_s(&tm, &t);

	std::ostringstream oss;
	oss << std::put_time(&tm, "%Y%m%d_%H%M%S");
	return oss.str();
}
