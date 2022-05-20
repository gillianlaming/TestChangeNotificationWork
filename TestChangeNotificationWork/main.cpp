#include <windows.h>
#include <iostream>
#include <wchar.h>
#include <thread>

#define MAX_TEST_CASES 256
#define ADD_TEST_CASE(f) AddTestCase(L#f, f)


bool waitfordebugger = FALSE;
int g_testCaseCount = 0;

struct TestCaseInfo
{
	bool(*function)(std::string* reason);
	std::wstring name;
};

struct TestCaseInfo g_testCases[MAX_TEST_CASES];

typedef HANDLE (* pfnEnterDetour)();
typedef void (* pfnLeaveDetour)();

pfnEnterDetour enterDetour;
pfnLeaveDetour leaveDetour;

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

void CreateNewFileInNestedSubdir() 
{
	//Add a brief sleep for synchronization
	Sleep(1000 * 10);

	//Open with delete on close so I don't have to do any cleanup
	HANDLE file = CreateFileW(L"D:\\home\\site\\wwwroot\\a\\b\\c\\d\\newfile.txt",
		DELETE,
		FILE_SHARE_READ | FILE_SHARE_DELETE,
		NULL,
		CREATE_NEW,
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE,
		NULL);

	if (file == INVALID_HANDLE_VALUE)
	{
		printf("CreatefileW (file) call failed with code %d", GetLastError());
	}
	else
	{
		CloseHandle(file);
	}
}

void CreateFileInWwwroot()
{
	//Add a brief sleep for synchronization
	Sleep(1000);

	HANDLE file = CreateFileW(L"D:\\home\\site\\wwwroot\\newfile123.txt",
		DELETE,
		FILE_SHARE_READ | FILE_SHARE_DELETE,
		NULL,
		CREATE_NEW,
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE,
		NULL);

	if (file == INVALID_HANDLE_VALUE)
	{
		printf("CreatefileW (file) call failed with code %d", GetLastError());
	}
	else
	{
		CloseHandle(file);
	}
}

#pragma endregion

#pragma region TestMethods

bool TestOpenRecentlyChangeDirHandleAddFile(std::string* reason)
/*+
	Open a recently changed directory. Directory is marked as recently changed by adding a file.
	Expected result: opene local directory
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
		return false;
	}
	 
	bool  isCachePath = IsDynamicCachePath(finalPath);

	if (!isCachePath)
	{
		*reason = FormattedString("Expected a local path but %ws is a remote path \n\n", finalPath);
		CloseHandle(file);
		CloseHandle(directory);
		return false;
	}

	CloseHandle(file);
	CloseHandle(directory);
	return true;
}

bool TestOpenRecentlyChangedDirectoryDeleteFile(std::string* reason)
/*
	Mark directory as recently changed by deleting file right before opening directory handle.
	Ensure the path is a local path.
*/
{
	//Delete file to mark directory as recently changed
	bool fRet = DeleteFileW(L"D:\\home\\site\\wwwroot\\newfile.txt");

	if (!fRet)
	{
		*reason = FormattedString("Delete file unexpectedly failed with error code %d \n\n", GetLastError());
		return false;
	}

	//Open parent directory handle
	HANDLE directory = CreateFileW(L"D:\\home\\site\\wwwroot",
		GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		NULL,
		OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS,
		NULL);

	if (directory == INVALID_HANDLE_VALUE)
	{
		*reason = FormattedString("CreatefileW (directory) call failed with code %d\n\n", GetLastError());
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
		return false;
	}

	bool  isCachePath = IsDynamicCachePath(finalPath);

	if (!isCachePath)
	{
		*reason = FormattedString("Expected a local path but %ws is a remote path \n\n", finalPath);
		CloseHandle(directory);
		return false;
	}

	CloseHandle(directory);
	return true;
}

bool TestOpenDirectoryNotRecentlyChanged(std::string* reason)
/*
	Open a directory which has not been recently changed to act as a control.
	Expects dynamic cache path
*/
{
	//Open a directory handle
	HANDLE directory = CreateFileW(L"D:\\home\\site\\wwwroot\\UnusedDirectory",
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
		return false;
	}

	bool  isCachePath = IsDynamicCachePath(finalPath);

	if (!isCachePath)
	{
		*reason = FormattedString("Expected a local path but %ws is a remote path \n\n", finalPath);
		CloseHandle(directory);
		return false;
	}

	CloseHandle(directory);
	return true;
}

bool TestQueryDirectoryFileOnRecentlyChangedDir(std::string* reason)
/*
	
	Call FindFirstFile on this which will open the directory and then call into NtQueryDirectoryFile
	With my changes, since the directory has been recently modified, the handle should be opened locally
	If the detour for NtQueryDirectoryFile were not implemented, it would return not found since dynamic cache will not contain this file. 
	The WEBSITE_DYNAMIC_CACHE_DELAY is set to 1 year, meaning updates to remote will not be propegated to dynamic cache until the remote content is 1 year old

*/
{	
	bool result = FALSE;
	HANDLE hFind;
	bool hr;

	enterDetour();
	//Create a new file which should not be cached
	HANDLE file = CreateFileW(L"D:\\home\\site\\wwwroot\\newfile1.txt",
		GENERIC_WRITE,
		0,
		NULL,
		OPEN_ALWAYS,
		0,
		NULL);

	leaveDetour();

	if (file == INVALID_HANDLE_VALUE)
	{
		*reason = FormattedString("CreatefileW (file) call failed with code %d", GetLastError());
		goto Finished;
	}

	//Call findfirst on this file
	WIN32_FIND_DATAA FindFileData;
	hFind = FindFirstFileA("D:\\home\\site\\wwwroot\\newfile1.txt", &FindFileData);

	if (hFind == INVALID_HANDLE_VALUE)
	{
		*reason = FormattedString("FindFirstFile failed (%d)\n", GetLastError());
		CloseHandle(file);
		DeleteFileW(L"D:\\home\\site\\wwwroot\\newfile1.txt");
		goto Finished;
	}
	else
	{
		printf(("The first file found is %s!!!\n\n"), FindFileData.cFileName);
		hr = FindClose(hFind);

		if (!hr)
		{
			printf("FindClose failed (%d)\n", GetLastError());
		}

		CloseHandle(file);
		result = true;
	}

Finished:

	DeleteFileW(L"D:\\home\\site\\wwwroot\\newfile1.txt");
	return result;
}

bool TestQueryDirectoryFileOnRecentlyChangedDirWithoutDetour(std::string* reason)
/*

	Call FindFirstFile on this which will open the directory and then call into NtQueryDirectoryFile
	With my changes, since the directory has been recently modified, the handle should be opened locally
	If the detour for NtQueryDirectoryFile were not implemented, it would return not found since dynamic cache will not contain this file.
	The WEBSITE_DYNAMIC_CACHE_DELAY is set to 1 year, meaning updates to remote will not be propegated to dynamic cache until the remote content is 1 year old

	logically this wont work. what i need to accomplish is preventing my detour for querydirectoryfile to be bypassed, but for the call to 
	createfilew (which is detoured) to be hit.
*/
{
	bool result = FALSE;
	HANDLE hFind;
	bool hr;

	enterDetour();
	//Create a new file which should not be cached
	HANDLE file = CreateFileW(L"D:\\home\\site\\wwwroot\\newfile2.txt",
			GENERIC_WRITE,
			0,
			NULL,
			CREATE_NEW,
			0,
			NULL);


	if (file == INVALID_HANDLE_VALUE)
	{
		*reason = FormattedString("CreatefileW (file) call failed with code %d", GetLastError());
		leaveDetour();
		goto Finished;
	}

	//Call findfirst on this file
	WIN32_FIND_DATAA FindFileData;
	hFind = FindFirstFileA("D:\\home\\site\\wwwroot\\newfile2.txt", &FindFileData);

	leaveDetour();
	if (hFind == INVALID_HANDLE_VALUE)
	{
		*reason = FormattedString("FindFirstFile failed (%d)\n", GetLastError());
		CloseHandle(file);
		DeleteFileW(L"D:\\home\\site\\wwwroot\\newfile1.txt");
		
		goto Finished;
	}
	else
	{
		printf(("The first file found is %s!!!\n\n"), FindFileData.cFileName);
		hr = FindClose(hFind);

		if (!hr)
		{
			printf("FindClose failed (%d)\n", GetLastError());
		}

		CloseHandle(file);
		result = true;
	}

Finished:

	DeleteFileW(L"D:\\home\\site\\wwwroot\\newfile2.txt");
	return result;
}

bool TestQueryDirectoryFileOnHydratedDir(std::string* reason)
/*
	Run findfirst on a file which exists in dynamic cache
*/
{
	//Call findfirst on this file
	WIN32_FIND_DATAA FindFileData;
	BOOL hr;
	HANDLE hFind = FindFirstFileA("D:\\home\\site\\wwwroot\\hostingstart.html", &FindFileData);

	if (hFind == INVALID_HANDLE_VALUE)
	{
		*reason = FormattedString("FindFirstFile failed (%d)\n", GetLastError());
		return false;
	}
	else
	{
		printf(("The first file found is %s!!!\n\n"), FindFileData.cFileName);
		hr = FindClose(hFind);

		if (!hr)
		{
			printf("FindClose failed (%d)\n", GetLastError());
		}

		return true;
	}
}

bool TestOpenDirectoryWithWrite(std::string* reason)
{
	//Open a directory handle
	HANDLE directory = CreateFileW(L"D:\\home\\site\\wwwroot",
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ,
		NULL,
		OPEN_EXISTING,
		FILE_LIST_DIRECTORY | FILE_FLAG_BACKUP_SEMANTICS,
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
		return false;
	}

	bool  isCachePath = IsDynamicCachePath(finalPath);

	if (isCachePath)
	{
		*reason = FormattedString("Expected a remote path but %ws is a local path \n\n", finalPath);
		CloseHandle(directory);
		return false;
	}

	CloseHandle(directory);
	return true;
}

bool TestStandardReadDirectoryChangesCall(std::string* reason)
/*+
	Test call to read directory changes with local handle.
	Note: for this test, you need to manually add a file.
	I tested this and it works if you add files to origin
*/
{
	//open directory handle to listen to change notifications on
	HANDLE directory = CreateFileW(L"D:\\home\\site\\wwwroot",
		FILE_LIST_DIRECTORY,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		NULL,
		OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
		NULL);

	if (directory == INVALID_HANDLE_VALUE)
	{
		*reason = FormattedString("CreatefileW (directory) call failed with code %d", GetLastError());
		return false;
	}

	WCHAR finalPath[MAX_PATH] = L"";
	DWORD dwRet;
	std::string errorMessage;
	OVERLAPPED overlapped = { 0 };
	DWORD dwBytesTransferred = 0;
	DWORD dwBytesReturned = 0;

	dwRet = EnterDetourAndGetFinalPathNameByHandle(
		directory,
		&errorMessage,
		finalPath,
		_countof(finalPath));

	if (dwRet == 0)
	{
		*reason = FormattedString("GetFinalPathNameByHandle failed with code %d, and because %s\n\n", GetLastError(), errorMessage.c_str());
		CloseHandle(directory);
		return false;
	}

	if (!IsDynamicCachePath(finalPath))
	{
		*reason = FormattedString("Expected local path, but instead listening for change notifications on a remote path. Path is %ws", finalPath);
		CloseHandle(directory);
		return false;
	}

	printf("Listening to change notifications on path %ws\n\n", finalPath);
	unsigned char buffer[8192];

	ZeroMemory(&overlapped, sizeof(overlapped));
	overlapped.hEvent = CreateEvent(NULL, /*manual reset*/ TRUE, /*initial state*/ FALSE, NULL);
	if (overlapped.hEvent == NULL)
	{
		*reason = FormattedString("Create event failed with code %d \n\n", GetLastError());
		return false;
	}

	//make a call to read directory changes
	dwRet = ReadDirectoryChangesW(
		directory,
		buffer,
		_countof(buffer),
		TRUE,
		FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE,
		&dwBytesReturned,
		&overlapped,
		NULL);

	std::thread thread(CreateFileInWwwroot);

	DWORD dwWaitStatus = WaitForSingleObject(overlapped.hEvent, 1000*60);

	thread.join();
	if (dwWaitStatus == WAIT_OBJECT_0)
	{
		dwRet = GetOverlappedResult(directory, &overlapped, &dwBytesTransferred, FALSE);

		if (!dwRet)
		{
			*reason = FormattedString("GetOverlappedResult failed with code %d\n\n", GetLastError());
			CloseHandle(directory);
			return false;
		}

		unsigned char* currentRecord = buffer;
		PFILE_NOTIFY_INFORMATION notifyInfo = (PFILE_NOTIFY_INFORMATION)currentRecord;

		WCHAR filename[MAX_PATH] = {0};

		DWORD action = notifyInfo->Action;

		// Copy out the file name as it is not null terminated.
		if (notifyInfo->FileNameLength < (MAX_PATH * sizeof(WCHAR)))
		{
			CopyMemory(filename, notifyInfo->FileName, notifyInfo->FileNameLength);

			printf("Change notification arrived for file %ws and action %d\n\n", filename, action);

			CloseHandle(overlapped.hEvent);
			CloseHandle(directory);
			//any other cleanup I have to do here?
			return TRUE;
		}
	}
	else
	{
		//assume timeout
		*reason = "ReadDirectoryChanges() call timed out";
		CloseHandle(overlapped.hEvent);
		CloseHandle(directory);
		return FALSE;
	}

	return FALSE;
}

bool TestReadDirectoryChangesNestedDirectory(std::string* reason)
/*
	Test ReadDirectoryChanges() call on a recently created nested directory (which may not exist in dynamic cache)

	Procedure:
		Create a new directory nested under wwwroot
		Call ReadDirectoryChanges on WWWROOT
		Create a file in recently created nested directory and see if change notification fires

	I delete this newly created directory at the end
*/
{
	BOOL result = FALSE;
	DWORD dwWaitStatus;
	WCHAR finalPath[MAX_PATH] = L"";
	std::string errorMessage;
	OVERLAPPED overlapped = { 0 };
	DWORD dwBytesTransferred = 0;
	DWORD dwBytesReturned = 0;
	std::thread thread(CreateNewFileInNestedSubdir);

	//Create a new directory very nested under wwwroot
	//Note: this is not pretty code...
	DWORD dwRet = CreateDirectoryW(L"D:\\home\\site\\wwwroot\\a", NULL) &&
				  CreateDirectoryW(L"D:\\home\\site\\wwwroot\\a\\b", NULL) &&
				  CreateDirectoryW(L"D:\\home\\site\\wwwroot\\a\\b\\c", NULL) &&
				  CreateDirectoryW(L"D:\\home\\site\\wwwroot\\a\\b\\c\\d", NULL);

	//brief sleep to prevent change notifiation firing
	Sleep(100);

	if (!dwRet)
	{
		*reason = FormattedString("Create new directory failed with %d", GetLastError());
		return result;
	}

	//open directory handle to listen to change notifications on
	HANDLE directory = CreateFileW(L"D:\\home\\site\\wwwroot",
		FILE_LIST_DIRECTORY,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		NULL,
		OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
		NULL);

	if (directory == INVALID_HANDLE_VALUE)
	{
		*reason = FormattedString("CreatefileW (directory) call failed with code %d", GetLastError());
		goto Finished;
	}

	dwRet = EnterDetourAndGetFinalPathNameByHandle(
		directory,
		&errorMessage,
		finalPath,
		_countof(finalPath));

	if (dwRet == 0)
	{
		*reason = FormattedString("GetFinalPathNameByHandle failed with code %d, and because %s\n\n", GetLastError(), errorMessage.c_str());
		goto Finished;
	}

	if (!IsDynamicCachePath(finalPath))
	{
		*reason = FormattedString("Expected local path, but instead listening for change notifications on a remote path. Path is %ws", finalPath);
		goto Finished;
	}

	printf("Listening to change notifications on path %ws\n\n", finalPath);
	unsigned char buffer[8192];

	ZeroMemory(&overlapped, sizeof(overlapped));
	overlapped.hEvent = CreateEvent(NULL, /*manual reset*/ TRUE, /*initial state*/ FALSE, NULL);
	if (overlapped.hEvent == NULL)
	{
		*reason = FormattedString("Create event failed with code %d \n\n", GetLastError());
		goto Finished;
	}

	//make a call to read directory changes
	dwRet = ReadDirectoryChangesW(
		directory,
		buffer,
		_countof(buffer),
		TRUE,
		FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE,
		&dwBytesReturned,
		&overlapped,
		NULL);

	dwWaitStatus = WaitForSingleObject(overlapped.hEvent, 1000 * 60);

	thread.join();

	if (dwWaitStatus == WAIT_OBJECT_0)
	{
		dwRet = GetOverlappedResult(directory, &overlapped, &dwBytesTransferred, FALSE);

		if (!dwRet)
		{
			*reason = FormattedString("GetOverlappedResult failed with code %d\n\n", GetLastError());
			goto Finished;
		}

		unsigned char* currentRecord = buffer;
		PFILE_NOTIFY_INFORMATION notifyInfo = (PFILE_NOTIFY_INFORMATION)currentRecord;

		WCHAR filename[MAX_PATH] = { 0 };

		DWORD action = notifyInfo->Action;

		// Copy out the file name as it is not null terminated.
		if (notifyInfo->FileNameLength < (MAX_PATH * sizeof(WCHAR)))
		{
			CopyMemory(filename, notifyInfo->FileName, notifyInfo->FileNameLength);

			printf("Change notification arrived for file %ws and action %d\n\n", filename, action);

			result = TRUE;
			goto Finished;
		}
	}
	else
	{
		//assume timeout
		*reason = "ReadDirectoryChanges() call timed out";
		goto Finished;
	}

Finished:
	if (overlapped.hEvent != NULL)
	{
		CloseHandle(overlapped.hEvent);
	}

	if (directory != INVALID_HANDLE_VALUE)
	{
		CloseHandle(directory);
	}

	dwRet = RemoveDirectory(L"D:\\home\\site\\wwwroot\\a\\b\\c\\d") &&
			RemoveDirectory(L"D:\\home\\site\\wwwroot\\a\\b\\c") &&
			RemoveDirectory(L"D:\\home\\site\\wwwroot\\a\\b") &&
			RemoveDirectory(L"D:\\home\\site\\wwwroot\\a");

	if (!dwRet)
	{
		printf("Delete directory failed with code %d\n\n", GetLastError());
	}
	
	return result;
}

bool TestCreateAndDeleteDirectory(std::string* reason)
/*
	Create a new directory at origin, then delete the directory to ensure it is delete from origin and not just locally
*/
{
	WCHAR finalPath[MAX_PATH] = L"";
	DWORD dwRet;
	std::string errorMessage;

	dwRet = CreateDirectoryW(L"D:\\home\\site\\wwwroot\\newDir", NULL);
	
	if (!dwRet)
	{
		*reason = FormattedString("Directory creation failed with code %d", GetLastError());
		return false;
	}

	//Open the directory handle
	HANDLE directory = CreateFileW(L"D:\\home\\site\\wwwroot\\newDir",
		GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL,
		OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS,
		NULL);
		
	if (directory == INVALID_HANDLE_VALUE)
	{
		*reason = FormattedString("CreatefileW (directory) call failed with code %d", GetLastError());
		return false;
	}

	dwRet = EnterDetourAndGetFinalPathNameByHandle(
		directory,
		&errorMessage,
		finalPath,
		_countof(finalPath));

	CloseHandle(directory);

	if (dwRet == 0)
	{
		*reason = FormattedString("GetFinalPathNameByHandle failed with code %d, and because %s\n\n", GetLastError(), errorMessage.c_str());
		return false;
	}

	if (!IsDynamicCachePath(finalPath))
	{
		*reason = FormattedString("Newly created directory handle is a remote handle %ws", finalPath);
		RemoveDirectory(L"D:\\home\\site\\wwwroot\\newDir");
		return false;
	}
	
	//now delete the directory and ensure it is cleaned up properly
	dwRet = RemoveDirectory(L"D:\\home\\site\\wwwroot\\newDir");
	if (!dwRet)
	{
		*reason = FormattedString("Directory deletion failed with code %d", GetLastError());
		return false;
	}


	return true;
}

bool TestWriteDirectoryAttributes(std::string* reason)
{
	//create a directory and mark it as old
	BOOL dwRet = CreateDirectoryW(L"D:\\home\\site\\wwwroot\\oldDir", NULL);

	if (!dwRet)
	{
		*reason = FormattedString("Directory creation failed with code %d", GetLastError());
		return false;
	}

	//Open the directory handle
	HANDLE directory = CreateFileW(L"D:\\home\\site\\wwwroot\\oldDir",
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_WRITE_ATTRIBUTES,
		NULL,
		OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS,
		NULL);

	WCHAR finalPath[MAX_PATH] = L"";
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
		RemoveDirectoryW(L"D:\\home\\site\\wwwroot\\oldDir");
		return false;
	}

	if (IsDynamicCachePath(finalPath))
	{
		*reason = FormattedString("Path is a dynamic cache when it should be a remote path %ss\n\n", finalPath);
		CloseHandle(directory);
		RemoveDirectoryW(L"D:\\home\\site\\wwwroot\\oldDir");
		return false;
	}

	FILE_BASIC_INFO fileInfo;

	if (!GetFileInformationByHandleEx(
		directory,
		FileBasicInfo,
		&fileInfo,
		sizeof(fileInfo)))
	{
		*reason = FormattedString("Failed to get file information: %d\n\n", GetLastError());
		CloseHandle(directory);
		RemoveDirectoryW(L"D:\\home\\site\\wwwroot\\oldDir");
		return FALSE;
	}

	SYSTEMTIME systemTime;
	GetSystemTime(&systemTime);
	systemTime.wYear = 1989;
	systemTime.wMonth = 1;
	systemTime.wDay = 1;
	FILETIME fileTime;
	SystemTimeToFileTime(&systemTime, &fileTime);

	// Set the timestamp to the beginning of the FILETIME times to make it look like created long time ago
	memcpy(&fileInfo.CreationTime, &fileTime, sizeof(FILETIME));
	memcpy(&fileInfo.LastWriteTime, &fileTime, sizeof(FILETIME));
	memcpy(&fileInfo.ChangeTime, &fileTime, sizeof(FILETIME));

	dwRet = SetFileInformationByHandle(directory, FileBasicInfo, &fileInfo, sizeof(fileInfo));

	if (!dwRet)
	{
		*reason = FormattedString("Failed to set file information with error %d", GetLastError());
		CloseHandle(directory);
		RemoveDirectoryW(L"D:\\home\\site\\wwwroot\\oldDir");
		return false;
	}

	CloseHandle(directory);
	//RemoveDirectoryW(L"D:\\home\\site\\wwwroot\\oldDir");
	return true;
}

bool TestRenameDirectory(std::string* reason)
{
	//Need to create a directory to rename
	/*
	BOOL dwRet = CreateDirectoryW(L"D:\\home\\site\\wwwroot\\directoryToRename", NULL);

	if (!dwRet)
	{
		*reason = FormattedString("Directory creation failed with code %d", GetLastError());
		return false;
	}
	*/
	DWORD dwRet = MoveFileExA(
				"D:\\home\\site\\wwwroot\\directoryToRename",
				"D:\\home\\site\\wwwroot\\RenamedDirectory",
				NULL);

	//ensure it exists remotely by detouring dynamic cache before opening
	enterDetour();

	HANDLE hRenamedDir = CreateFileW(L"D:\\home\\site\\wwwroot\\oldDir",
								GENERIC_READ,
								FILE_SHARE_READ,
								NULL,
								OPEN_EXISTING,
								FILE_FLAG_BACKUP_SEMANTICS,
								NULL);
	leaveDetour();

	WCHAR finalPath[MAX_PATH] = L"";
	std::string errorMessage;

	dwRet = EnterDetourAndGetFinalPathNameByHandle(
		hRenamedDir,
		&errorMessage,
		finalPath,
		_countof(finalPath));

	if (dwRet == 0)
	{
		*reason = FormattedString("GetFinalPathNameByHandle failed with code %d, and because %s\n\n", GetLastError(), errorMessage.c_str());
		CloseHandle(hRenamedDir);
		return false;
	}

	bool  isCachePath = IsDynamicCachePath(finalPath);

	if (isCachePath)
	{
		*reason = FormattedString("Expected a remote path but %ws is a local path \n\n", finalPath);
		CloseHandle(hRenamedDir);
		return false;
	}

	//need to ensure cache is hydrated thoug

	return true;
}

#pragma endregion

#pragma region Main Execution methods

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
		fflush(stdout);
	}

	printf("%d/%d tests passed(%.2f%%)\n", passedTests, totalTests, ((float)passedTests / (float)totalTests) * 100.0f);
}

bool RunSetup()
{
	HMODULE module = LoadLibrary(L"picohelper.dll");

	if (module == NULL)
	{
		printf("Could not load module picohelper.dll. Failed with: %d\n", GetLastError());
		return false;
	}

	enterDetour = (pfnEnterDetour)GetProcAddress(module, "TestEnterDynamicCacheDetour");
	leaveDetour = (pfnLeaveDetour)GetProcAddress(module, "TestLeaveDynamicCacheDetour");

	if (enterDetour == NULL || leaveDetour == NULL)
	{
		printf("get proc address failed with %d \n", GetLastError());
		return false;
	}

	//TODO: delete any files/directories if they exist from previous runs
	//It's okay if these fail -- they might not exist so failure would be expected.
	RemoveDirectoryW(L"D:\\home\\site\\wwwroot\\oldDir");

	return true;
}

int main()
{

	if (waitfordebugger)
	{
		printf("Waiting for 60 seconds for the debugger to be attached \n");
		fflush(stdout);
		Sleep(1000 * 60);

		printf("Done waiting for the debugger \n");
		fflush(stdout);
	}

	bool hr = RunSetup();

	if (!hr)
	{
		printf("Setup failed with code %d. Aborting tests.\n\n", GetLastError());
	}

	//Note: these tests are in a specific order. Please do not reorder them. Otherwise synchronization could be messed up
	ADD_TEST_CASE(TestOpenDirectoryNotRecentlyChanged);
	ADD_TEST_CASE(TestOpenRecentlyChangeDirHandleAddFile);
	ADD_TEST_CASE(TestOpenRecentlyChangedDirectoryDeleteFile);
	ADD_TEST_CASE(TestQueryDirectoryFileOnRecentlyChangedDir);
	//ADD_TEST_CASE(TestQueryDirectoryFileOnRecentlyChangedDirWithoutDetour);
	ADD_TEST_CASE(TestQueryDirectoryFileOnHydratedDir);
	ADD_TEST_CASE(TestOpenDirectoryWithWrite);
	ADD_TEST_CASE(TestStandardReadDirectoryChangesCall);
	ADD_TEST_CASE(TestReadDirectoryChangesNestedDirectory);
	ADD_TEST_CASE(TestCreateAndDeleteDirectory);
	ADD_TEST_CASE(TestWriteDirectoryAttributes);
	ADD_TEST_CASE(TestRenameDirectory);
	//Test readdirectorychanges on newly created directory
	//test rename directory

	
	RunTests();
}

#pragma endregion