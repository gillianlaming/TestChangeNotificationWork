#include <windows.h>
#include <iostream>
#include <wchar.h>

#define MAX_TEST_CASES 256
#define ADD_TEST_CASE(f) AddTestCase(L#f, f)


bool waitfordebugger = false;
int g_testCaseCount = 0;

struct TestCaseInfo
{
	bool(*function)(std::string* reason);
	std::wstring name;
};

struct TestCaseInfo g_testCases[MAX_TEST_CASES];

typedef HANDLE (* pfnEnterDetour)();
typedef void (* pfnLeaveDetour)();

#pragma region Helper Methods

std::string FormattedString(const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	char message[1024];
	vsnprintf_s(message, 1024, fmt, args);
	va_end(args);

	// May cause more copying than necessary, but looks safer.
	std::string strMessage = message;
	return strMessage;
}

bool EnterDetourAndGetFinalPathNameByHandle(
	_In_ HANDLE handle,
	_In_ std::string* reason,
	_Out_writes_(cchFilePath) LPWSTR lpszFilePath,
	_In_ DWORD cchFinalPath)
{
	HMODULE module = LoadLibrary(L"picohelper.dll");

	if (module == NULL)
	{
		*reason = FormattedString("Could not load module picohelper.dll. Failed with: %d\n", GetLastError());
		return false;
	}

	pfnEnterDetour enterDetour = (pfnEnterDetour)GetProcAddress(module, "TestEnterDynamicCacheDetour");
	pfnLeaveDetour leaveDetour = (pfnLeaveDetour)GetProcAddress(module, "TestLeaveDynamicCacheDetour");

	if (enterDetour == NULL || leaveDetour == NULL)
	{
		*reason = FormattedString("get proc address failed with %d \n", GetLastError());
		return false;
	}

	enterDetour();

	//TODO: check to see if this is detoured anywhere
	DWORD retVal = GetFinalPathNameByHandleW(handle, lpszFilePath, cchFinalPath, 0);

	if (retVal == 0)
	{
		*reason = FormattedString("getfinalpathnamebyhandle failed with %d\n", GetLastError());
		return false;
	}

	leaveDetour();
	return true;
}

void AddTestCase(std::wstring name, bool(*func)(std::string* reason))
{
	g_testCases[g_testCaseCount].function = func;
	g_testCases[g_testCaseCount].name = name;
	g_testCaseCount++;
}

bool IsDynamicCachePath(PWSTR path)
/*+
	Determines is specified path is a dynamic cache path
*/
{
	PWSTR cachePath = wcsstr(path, L"DynamicCache");

	if (cachePath != NULL)
	{
		return true;
	}

	return false;
}

#pragma endregion

#pragma region TestMethods

bool TestOpenRecentlyChangeDirHandleAddFile(std::string* reason)
/*+
	Open a recently changed directory
*/
{
	HANDLE file = CreateFileW(L"D:\\home\\site\\wwwroot\\newfile.txt",
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ,
		NULL,
		CREATE_NEW,
		FILE_ATTRIBUTE_NORMAL,
		NULL);


	if (file == INVALID_HANDLE_VALUE)
	{
		*reason = FormattedString("CreatefileW (file) call failed with code %d", GetLastError());
		return false;
	}

	//Open a directory handle
	HANDLE directory = CreateFileW(L"D:\\home\\site\\wwwroot",
		GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		NULL,
		OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS,
		NULL);

	if (directory == INVALID_HANDLE_VALUE)
	{
		*reason = FormattedString("CreatefileW (directory) call failed with code %d", GetLastError());
		return false;
	}

	WCHAR finalPath[MAX_PATH] = L"";
	DWORD dwRet;
	std::string errorMessage;

	dwRet = EnterDetourAndGetFinalPathNameByHandle(
		directory,
		&errorMessage,
		finalPath,
		_countof(finalPath));

	if (dwRet == 0)
	{
		*reason = FormattedString("GetFinalPathNameByHandle failed with code %d, and because %s\n\n", GetLastError(), errorMessage.c_str());
		CloseHandle(directory);
		CloseHandle(file);
		BOOL del = DeleteFileW(L"D:\\home\\site\\wwwroot\\newfile.txt");
		return false;
	}
	 
	bool  isCachePath = IsDynamicCachePath(finalPath);

	if (!isCachePath)
	{
		*reason = FormattedString("Expected a local path but %ws is not \n\n", finalPath);
		CloseHandle(file);
		DeleteFileW(L"D:\\home\\site\\wwwroot\\newfile.txt");
		CloseHandle(directory);
		return false;
	}

	CloseHandle(file);
	DeleteFileW(L"D:\\home\\site\\wwwroot\\newfile.txt");
	CloseHandle(directory);
	return true;
}

bool TestOpenRecentlyChangedDirectoryDeleteFile(std::string* reason)
{
	printf("do something \n\n");
	*reason = "Unexpected error calling TestOpenRecentlyChangedDirectoryDeleteFile";
	return false;
}

bool TestOpenDirectoryNotRecentlyChanged(std::string* reason)
{
	return TRUE;
}

bool TestQueryDirectoryFileOnRecentlyChangedDir(std::string* reason)
{

	//
	//Call FindFirstFile on this which will open the directory and then call into NtQueryDirectoryFile
	//With my changes, since the directory has been recently modified, the handle should be opened locally
	//If the detour for NtQueryDirectoryFile were not implemented, it would return not found since dynamic cache will not contain this file. 
	//The WEBSITE_DYNAMIC_CACHE_DELAY is set to 1 year, meaning updates to remote will not be propegated to dynamic cache until the remote content is 1 year old
	//
	/*
	* WIN32_FIND_DATAA FindFileData;
	HANDLE hFind = FindFirstFileA("D:\\home\\site\\wwwroot\\newfile.txt", &FindFileData);

	if (hFind == INVALID_HANDLE_VALUE)
	{
		printf("FindFirstFile failed (%d)\n", GetLastError());
		CloseHandle(file);
		BOOL del = DeleteFileW(L"D:\\home\\site\\wwwroot\\newfile.txt");
		return false;
	}
	else
	{
		printf(("The first file found is %s!!!\n\n"), FindFileData.cFileName);
		bool ret = CancelIoEx(directory, NULL);
		printf("CancelIoEx returned %d \n\n", ret);
		FindClose(hFind);
		CloseHandle(file);
	}

	BOOL del = DeleteFileW(L"D:\\home\\site\\wwwroot\\newfile.txt");

	
	retVal = GetFinalPathNameByHandleW(file, filePath, _countof(filePath), 0);

	if (retVal == 0)
	{
		printf("getfinalpathnamebyhandle failed with %d", GetLastError());
		return -1;
	}

	printf("\n\nThe path to the FILE HANDLE IS:");
	wprintf(filePath);
	*/
	return true;
}

bool TestQueryDirectoryFileOnHydratedDir(std::string* reason)
{
	return true;
}

#pragma endregion

void RunTests()
{
	int totalTests = 0;
	int passedTests = 0;

	printf("Starting to run all tests....\n\n"); 
	fflush(stdout);

	for (int i = 0; i < g_testCaseCount; i++)
	{
		bool passed = false;

		printf("START : %S\n", g_testCases[i].name.c_str());
		fflush(stdout);
		std::string reason;
		bool result = g_testCases[i].function(&reason);

		if (result)
		{
			printf("PASSED: %S\n", g_testCases[i].name.c_str());
			fflush(stdout);
			passed = true;
		}
		else
		{
			printf("FAILED: %S: %s \n", g_testCases[i].name.c_str(), reason.c_str());
			fflush(stdout);
		}

		if (passed)
		{
			passedTests++;
		}

		totalTests++;
		printf("\n");
	}

	printf("%d/%d tests passed(%.2f%%)\n", passedTests, totalTests, ((float)passedTests / (float)totalTests) * 100.0f);
}

int main()
{

	if (waitfordebugger)
	{
		printf("Waiting for 30 seconds for the debugger to be attached \n");
		fflush(stdout);
		Sleep(1000 * 60);

		printf("Done waiting for the debugger \n");
		fflush(stdout);
	}

	ADD_TEST_CASE(TestOpenRecentlyChangeDirHandleAddFile);
	ADD_TEST_CASE(TestOpenRecentlyChangedDirectoryDeleteFile);
	ADD_TEST_CASE(TestOpenDirectoryNotRecentlyChanged);
	ADD_TEST_CASE(TestQueryDirectoryFileOnRecentlyChangedDir);
	ADD_TEST_CASE(TestQueryDirectoryFileOnHydratedDir);
	
	RunTests();
}