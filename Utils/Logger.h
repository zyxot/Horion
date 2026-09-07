#pragma once

#include <cstdarg>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#ifdef _DEBUG
#include <ios>
#include <iosfwd>
#include <iostream>
#endif

#include "xorstr.h"

#pragma comment(lib, "runtimeobject")

// Compatibility logger used while modernizing the archived client.
// The old logger depends on the UWP ApplicationData roaming folder, which may
// fail before we get useful crash diagnostics on newer GDK-based Bedrock builds.
// Keep the trace somewhere independent of Minecraft's package/storage model.
inline void HorionCompatLog(volatile char* fmt, ...) {
	char message[2048] = {};

	va_list args;
	va_start(args, fmt);
	vsnprintf_s(message, sizeof(message), _TRUNCATE, const_cast<const char*>(fmt), args);
	va_end(args);

	char tempPath[MAX_PATH] = {};
	char logPath[MAX_PATH] = {};
	DWORD tempLen = GetTempPathA(MAX_PATH, tempPath);
	if (tempLen > 0 && tempLen < MAX_PATH)
		sprintf_s(logPath, "%sHorionCompat.log", tempPath);
	else
		strcpy_s(logPath, "HorionCompat.log");

	FILE* file = nullptr;
	if (fopen_s(&file, logPath, "a") == 0 && file != nullptr) {
		SYSTEMTIME now{};
		GetLocalTime(&now);
		fprintf(file, "[%02u:%02u:%02u.%03u] %s\n", now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, message);
		fclose(file);
	}

	OutputDebugStringA("[Horion] ");
	OutputDebugStringA(message);
	OutputDebugStringA("\n");
}

#ifndef logF
#define logF(x, ...) HorionCompatLog(XorString(x), __VA_ARGS__)
#endif

struct TextForPrint {
	char time[20];
	char text[100];
};

struct TextForPrintBig {
	char time[20];
	char text[2900];
};

class Logger {

public:
	static bool isActive();
	static std::wstring GetRoamingFolderPath();
	static void WriteLogFileF(volatile char* fmt, ...);
	static void WriteBigLogFileF(size_t maxSize, const char* fmt, ...);
	static void SendToConsoleF(const char* msg);
	static std::vector<TextForPrint>* GetTextToPrint();
	static std::vector<std::shared_ptr<TextForPrintBig>>* GetTextToSend();
	static std::lock_guard<std::mutex> GetTextToPrintLock();
	static std::lock_guard<std::mutex> GetTextToInjectorLock();
	//static std::vector<TextForPrint*> stringPrintVector;
	static void Disable();
};
