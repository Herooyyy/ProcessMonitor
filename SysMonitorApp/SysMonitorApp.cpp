
#include <Windows.h>
#include "..\SysMonitor\SysMonCommon.h"
#include <string>

int Error(const char* text) {
	printf("%s (%d)\n", text, GetLastError());
	return 1;
}

void DisplayTime(const LARGE_INTEGER& time) {
	SYSTEMTIME st;
	FileTimeToSystemTime((FILETIME*)&time, &st);
	printf("%02d:%02d:%02d.%03d: ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
}

void DisplayInfo(BYTE* buffer, DWORD size) {
	auto count = size;
	while (count > 0) {
		auto header = (ItemHeader*)buffer;
		switch (header->Type) {
		case ItemType::ProcessExit:
		{
			DisplayTime(header->Time);
			auto info = (ProcessExitInfo*)buffer;
			printf("Process %d Exited\n", info->ProcessId);
			break;
		}

		case ItemType::ProcessCreate:
		{
			DisplayTime(header->Time);
			auto info = (ProcessCreateInfo*)buffer;
			std::wstring commandline((WCHAR*)(buffer + info->CommandLineOffset), info->CommandLineLength);
			printf("Process %d Created. Command line: %ws\n", info->ProcessId, commandline.c_str());
			break;
		}
		case ItemType::ThreadCreate:
		{
			DisplayTime(header->Time);
			auto info = (ThreadCreateExitInfo*)buffer;
			printf("Thread %d Created in process %d\n",
				info->ThreadId, info->ProcessId);
			break;
		}
		case ItemType::ThreadExit:
		{
			DisplayTime(header->Time);
			auto info = (ThreadCreateExitInfo*)buffer;
			printf("Thread %d Exited from process %d\n",
				info->ThreadId, info->ProcessId);
			break;
		}
		case ItemType::ImageLoad:
		{
			DisplayTime(header->Time);
			auto info = (ImageLoadInfo*)buffer;
			std::wstring imagename((WCHAR*)(buffer + info->ImagePathOffset), info->ImagePathLength);
			printf("Image loaded in process %d : %ws\n", info->ProcessId, imagename.c_str());
			break;
		}
		case ItemType::RemoteThreadCreate:
		{
			DisplayTime(header->Time);
			auto info = (RemoteThreadCreateInfo*)buffer;
			printf("Process %d created a remote thread %d on process %d\n", info->SourceProcessId, info->TargetProcessId, info->ThreadId);
			break;
		}

		default:
			break;
		}
		buffer += header->Size;
		count -= header->Size;
	}

}

int main() {
	auto hFile = CreateFile(L"\\\\.\\SysMon", GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
	if (hFile == INVALID_HANDLE_VALUE)
		return Error("Failed to open file");

	while (true) {
		BYTE buffer[1 << 16];
		DWORD bytes;
		if (!ReadFile(hFile, buffer, sizeof(buffer), &bytes, nullptr))
			return Error("Failed to read");

		if (bytes != 0)
			DisplayInfo(buffer, bytes);

		Sleep(200);
	}
}
