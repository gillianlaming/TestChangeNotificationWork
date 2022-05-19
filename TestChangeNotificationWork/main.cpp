#include <windows.h>
#include <iostream>

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

bool TestCreateFile(std::string* reason)
/*+
	Test createfilew calls with a variety of different flags.
	The expected behavior is commented above each test
*/
{
	WIN32_FIND_DATAA FindFileData;

	//Create a new file
	HANDLE file = CreateFileW(L"D:\\home\\site\\wwwroot\\newfile.txt",
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ,
		NULL,
		CREATE_NEW,
		FILE_ATTRIBUTE_NORMAL,
		NULL);

	//try closing the file

	if (file == INVALID_HANDLE_VALUE)
	{
		printf("CreatefileW (file) call failed with code %d", GetLastError());
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
		printf("CreatefileW (directory) call failed with code %d", GetLastError());
		return false;
	}

	//
	//Call FindFirstFile on this which will open the directory and then call into NtQueryDirectoryFile
	//With my changes, since the directory has been recently modified, the handle should be opened locally
	//If the detour for NtQueryDirectoryFile were not implemented, it would return not found since dynamic cache will not contain this file. 
	//The WEBSITE_DYNAMIC_CACHE_DELAY is set to 1 year, meaning updates to remote will not be propegated to dynamic cache until the remote content is 1 year old
	//
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


	HMODULE module = LoadLibrary(L"picohelper.dll");

	if (module == NULL)
	{
		printf("cOULD NOT LOad module failed with %d", GetLastError());
		return false;
	}

	pfnEnterDetour enterDetour = (pfnEnterDetour)GetProcAddress(module, "TestEnterDynamicCacheDetour");
	pfnLeaveDetour leaveDetour = (pfnLeaveDetour)GetProcAddress(module, "TestLeaveDynamicCacheDetour");

	if (enterDetour == NULL || leaveDetour == NULL)
	{
		printf("get proc address failed with %d", GetLastError());
		return false;
	}

	enterDetour();
	WCHAR filePath[256];

	//TODO: check to see if this is detoured anywhere
	DWORD retVal = GetFinalPathNameByHandleW(directory, filePath, _countof(filePath), 0);

	if (retVal == 0)
	{
		printf("getfinalpathnamebyhandle failed with %d\n", GetLastError());
		return false;
	}

	printf("The path to the DIRECTORY HANDLE IS: %ws \n\n", filePath);

	BOOL del = DeleteFileW(L"D:\\home\\site\\wwwroot\\newfile.txt");

	/*
	retVal = GetFinalPathNameByHandleW(file, filePath, _countof(filePath), 0);

	if (retVal == 0)
	{
		printf("getfinalpathnamebyhandle failed with %d", GetLastError());
		return -1;
	}

	printf("\n\nThe path to the FILE HANDLE IS:");
	wprintf(filePath);
	*/
	leaveDetour();
	CloseHandle(directory);
	return true;
}

bool TestSomethingElse(std::string* reason)
{
	printf("do something \n\n");
	*reason = "Unexpected error calling TestSomethingElse";
	return false;
}

void AddTestCase(std::wstring name, bool(*func)(std::string* reason))
{
	g_testCases[g_testCaseCount].function = func;
	g_testCases[g_testCaseCount].name = name;
	g_testCaseCount++;
}

void RunTests()
{
	int totalTests = 0;
	int passedTests = 0;

	for (int i = 0; i < g_testCaseCount; i++)
	{
		bool passed = false;

		printf("START : %S\n", g_testCases[i].name.c_str());

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
		Sleep(1000 * 60);

		printf("Done waiting for the debugger \n");
	}

	ADD_TEST_CASE(TestSomethingElse);
	ADD_TEST_CASE(TestCreateFile);

	
	
	RunTests();
}