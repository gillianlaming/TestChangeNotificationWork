#include <windows.h>
#include <iostream>


typedef HANDLE (* pfnEnterDetour)();
typedef void (* pfnLeaveDetour)();

int main()
{
	//add 60 second sleep to attach to debugger
	Sleep(1000 * 60);
	
	//Open a directory handle
	HANDLE directory = CreateFileW(L"D:\\home\\site\\wwwroot",
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ,
		NULL,
		OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS,                                                                                                                                                                   
		NULL);

	//todo: dump attributes here too?

	if (directory == INVALID_HANDLE_VALUE)
	{
		printf("CreatefileW (directory) call failed with code %d", GetLastError());
		return -1;
	}

	//open a file handle
	HANDLE file = CreateFileW(L"D:\\home\\site\\wwwroot\\testing.txt",
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ,
		NULL,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		NULL);

	if (file == INVALID_HANDLE_VALUE)
	{
		printf("CreatefileW (file) call failed with code %d", GetLastError());
		return -1;
	}

	HMODULE module = LoadLibrary(L"picohelper.dll");

	if (module == NULL)
	{
		printf("cOULD NOT LOad module failed with %d", GetLastError());
		return -1;
	}

	pfnEnterDetour enterDetour = (pfnEnterDetour)GetProcAddress(module, "TestEnterDynamicCacheDetour");
	pfnLeaveDetour leaveDetour = (pfnLeaveDetour)GetProcAddress(module, "TestLeaveDynamicCacheDetour");
	
	if (enterDetour == NULL || leaveDetour == NULL)
	{
		printf("get proc address failed with %d", GetLastError());
		return -1;
	}

	enterDetour();
	WCHAR filePath[256];
	
	DWORD retVal = GetFinalPathNameByHandleW(directory, filePath, _countof(filePath), 0);

	if (retVal == 0)
	{
		printf("getfinalpathnamebyhandle failed with %d", GetLastError());
		return -1;
	}
	
	printf("The path to the DIRECTORY HANDLE IS:");
	wprintf(filePath);

	retVal = GetFinalPathNameByHandleW(file, filePath, _countof(filePath), 0);

	if (retVal == 0)
	{
		printf("getfinalpathnamebyhandle failed with %d", GetLastError());
		return -1;
	}

	printf("\n\nThe path to the FILE HANDLE IS:");
	wprintf(filePath);

	leaveDetour();
	CloseHandle(directory);
	CloseHandle(file);
}