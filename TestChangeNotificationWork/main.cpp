#include <windows.h>
#include <iostream>
#include <wchar.h>
#include <thread>

#define MAX_TEST_CASES 256
#define ADD_TEST_CASE(f) AddTestCase(L#f, f)


bool waitfordebugger = FALSE;
bool oneTimeEnvSetupForTests = FALSE;
bool verboseLoggingEnabled = TRUE;
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
	_Out_writes_(cchFilePath) LPWSTR lpszFilePath,
	_In_ DWORD cchFinalPath)
{
	enterDetour();

	//TODO: check to see if this is detoured anywhere
	DWORD retVal = GetFinalPathNameByHandleW(handle, lpszFilePath, cchFinalPath, 0);

	if (retVal == 0)
	{
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

void CreateAndDeleteFile(LPCWSTR path)
{
	HANDLE file = CreateFileW(path,
		DELETE,
		FILE_SHARE_READ | FILE_SHARE_DELETE,
		NULL,
		CREATE_NEW,
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE,
		NULL);

	if (file == INVALID_HANDLE_VALUE)
	{
		printf("CreatefileW (file) call failed with code %d \n", GetLastError());
	}
	else
	{
		CloseHandle(file);
	}
}

void CreateFile(LPCWSTR path)
{
	HANDLE file = CreateFileW(path,
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ ,
		NULL,
		CREATE_NEW,
		FILE_ATTRIBUTE_NORMAL,
		NULL);

	if (file == INVALID_HANDLE_VALUE)
	{
		printf("CreatefileW (file) call failed with code %d \n", GetLastError());
	}
	else
	{
		CloseHandle(file);
	}
}

void CreateManyFiles()
{
	WCHAR path[MAX_PATH];
	for (int i = 0; i < 2000; i++)
	{
		GetTempFileNameW(L"D:\\home\\site\\wwwroot\\Dir2", L"fil", i, path);
		CreateFile(path);
	}

	//CreateAndDeleteFile(path);
}

#pragma endregion

#pragma region TestMethods

bool TestOpenRecentlyChangeDirHandleAddFile(std::string* reason)
/*+
	Open a recently changed directory. Directory is marked as recently changed by adding a file.
	Expected result: opene local directory
*/
{
	if (verboseLoggingEnabled)
	{
		printf("Creating a new file wwwroot\\TestDirectories\\TestOpenDirRecentlyChanged1\\newfile.txt. This will mark parent directory as recently changed.\n\n");
	}

	HANDLE file = CreateFileW(L"D:\\home\\site\\wwwroot\\TestDirectories\\TestOpenDirRecentlyChanged1\\newfile.txt",
		GENERIC_READ | GENERIC_WRITE | DELETE,
		FILE_SHARE_READ | FILE_SHARE_DELETE,
		NULL,
		CREATE_NEW,
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE,
		NULL);


	if (file == INVALID_HANDLE_VALUE)
	{
		*reason = FormattedString("CreatefileW (file) call failed with code %d\n\n", GetLastError());
		return false;
	}

	if (verboseLoggingEnabled)
	{
		printf("Opening handle to parent directory: D:\\home\\site\\wwwroot\\TestDirectories\\TestOpenDirRecentlyChanged1 \n\n");
	}

	//Open a directory handle
	HANDLE directory = CreateFileW(L"D:\\home\\site\\wwwroot\\TestDirectories\\TestOpenDirRecentlyChanged1",
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

	WCHAR finalPath[MAX_PATH] = L"";
	DWORD dwRet;

	dwRet = EnterDetourAndGetFinalPathNameByHandle(
		directory,
		finalPath,
		_countof(finalPath));

	if (dwRet == 0)
	{
		*reason = FormattedString("GetFinalPathNameByHandle failed with code %d\n\n", GetLastError());
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

	if (verboseLoggingEnabled)
	{
		printf("Path to handle opened is a local path %ws \n\n", finalPath);
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
	HANDLE file = CreateFileW(L"D:\\home\\site\\wwwroot\\TestDirectories\\TestOpenDirRecentlyChanged2\\newfile.txt",
		GENERIC_READ | GENERIC_WRITE | DELETE,
		FILE_SHARE_READ | FILE_SHARE_DELETE,
		NULL,
		CREATE_NEW,
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE,
		NULL);

	if (file == INVALID_HANDLE_VALUE)
	{
		*reason = FormattedString("Delete file unexpectedly failed with error code %d \n\n", GetLastError());
		return false;
	}

	CloseHandle(file);

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

	dwRet = EnterDetourAndGetFinalPathNameByHandle(
		directory,
		finalPath,
		_countof(finalPath));

	if (dwRet == 0)
	{
		*reason = FormattedString("GetFinalPathNameByHandle failed with code %d\n\n", GetLastError());
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
	HANDLE directory = CreateFileW(L"D:\\home\\site\\wwwroot\\TestDirectories\\UnusedDirectory",
		GENERIC_READ,
		FILE_SHARE_READ,
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

	dwRet = EnterDetourAndGetFinalPathNameByHandle(
		directory,
		finalPath,
		_countof(finalPath));

	if (dwRet == 0)
	{
		*reason = FormattedString("GetFinalPathNameByHandle failed with code %d\n\n", GetLastError());
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
	Tests FindFirstFile and FindNextFile

	Call FindFirstFile on this which will open the directory and then call into NtQueryDirectoryFile
	With my changes, since the directory has been recently modified, the handle should be opened locally
	If the detour for NtQueryDirectoryFile were not implemented, it would return not found since dynamic cache will not contain this file. 
	
*/
{	
	bool result = FALSE;
	HANDLE hFind;
	HANDLE file2 = INVALID_HANDLE_VALUE;
	bool hr;

	enterDetour();
	//Create two new files which should not be cached
	HANDLE file = CreateFileW(L"D:\\home\\site\\wwwroot\\TestDirectories\\FindFirst\\newfile1.txt",
		GENERIC_WRITE,
		0,
		NULL,
		OPEN_ALWAYS,
		0,
		NULL);

	HANDLE file1 = CreateFileW(L"D:\\home\\site\\wwwroot\\TestDirectories\\FindFirst\\newfile2.txt",
		GENERIC_WRITE,
		0,
		NULL,
		OPEN_ALWAYS,
		0,
		NULL);

	leaveDetour();

	if (file == INVALID_HANDLE_VALUE || file1 == INVALID_HANDLE_VALUE)
	{
		*reason = FormattedString("CreatefileW (file) call failed with code %d", GetLastError());
		goto Finished;
	}
	
	//Mark the directory as recently changed by creating and deleting a file
	file2 = CreateFileW(L"D:\\home\\site\\wwwroot\\TestDirectories\\FindFirst\\newfile3.txt",
		GENERIC_WRITE | GENERIC_WRITE,
		FILE_SHARE_READ,
		NULL,
		OPEN_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		NULL);

	CloseHandle(file2);

	//Call findfirst on file with .txt 
	WIN32_FIND_DATAA FindFileData;
	hFind = FindFirstFileA("D:\\home\\site\\wwwroot\\TestDirectories\\FindFirst\\*.txt", &FindFileData);

	if (hFind == INVALID_HANDLE_VALUE)
	{
		*reason = FormattedString("FindFirstFile failed (%d)\n", GetLastError());
		goto Finished;
	}
	else
	{
		printf(("The first file found is %s!!!\n\n"), FindFileData.cFileName);

		//Call findnext using the same handle
		FindNextFileA(hFind, &FindFileData);
		
		if (hFind == INVALID_HANDLE_VALUE)
		{
			*reason = FormattedString("FindNextFile failed (%d)\n", GetLastError());
			FindClose(hFind);
			goto Finished;
		}

		printf(("FindNextFile found %s!!!\n\n"), FindFileData.cFileName);

		hr = FindClose(hFind);
		if (!hr)
		{
			printf("FindClose failed (%d)\n", GetLastError());
		}
		
		result = true;
	}

Finished:

	CloseHandle(file);
	CloseHandle(file1);
	DeleteFileW(L"D:\\home\\site\\wwwroot\\TestDirectories\\FindFirst\\newfile1.txt");
	DeleteFileW(L"D:\\home\\site\\wwwroot\\TestDirectories\\FindFirst\\newfile2.txt");
	DeleteFileW(L"D:\\home\\site\\wwwroot\\TestDirectories\\FindFirst\\newfile3.txt");
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
	Run findfirst on a file within a directory which has not been touched
*/
{
	//Call findfirst on this file
	WIN32_FIND_DATAA FindFileData;
	BOOL hr;
	HANDLE hFind = FindFirstFileA("D:\\home\\site\\wwwroot\\TestDirectories\\FindFirstHydrated\\newfile.txt", &FindFileData);

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
	HANDLE directory = CreateFileW(L"D:\\home\\site\\wwwroot\\TestDirectories\\DirectoryOpenForWrite",
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

	dwRet = EnterDetourAndGetFinalPathNameByHandle(
		directory,
		finalPath,
		_countof(finalPath));

	if (dwRet == 0)
	{
		*reason = FormattedString("GetFinalPathNameByHandle failed with code %d\n\n", GetLastError());
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

bool TestReadDirectoryChangesNoDetour(std::string* reason)
/*+
	Assert handle is opened locally
	Call ReadDirectoryChanges on this local handle
	Trigger a change notification and expect a result from this change

	This code is not detoured so it will behave like a customer app 
*/
{
	if (verboseLoggingEnabled)
	{
		printf("Opening handle to directory D:\\home\\site\\wwwroot\\TestDirectories\\StandardRDC \n\n");
	}

	// open directory handle to listen to change notifications on
	HANDLE directory = CreateFileW(L"D:\\home\\site\\wwwroot\\TestDirectories\\StandardRDC",
		FILE_LIST_DIRECTORY,
		FILE_SHARE_READ,
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
	OVERLAPPED overlapped = { 0 };
	DWORD dwBytesTransferred = 0;
	DWORD dwBytesReturned = 0;

	dwRet = EnterDetourAndGetFinalPathNameByHandle(
		directory,
		finalPath,
		_countof(finalPath));

	if (dwRet == 0)
	{
		*reason = FormattedString("GetFinalPathNameByHandle failed with code %d\n\n", GetLastError());
		CloseHandle(directory);
		return false;
	}

	if (!IsDynamicCachePath(finalPath))
	{
		*reason = FormattedString("Expected local path, but instead listening for change notifications on a remote path. Path is %ws", finalPath);
		CloseHandle(directory);
		if (!verboseLoggingEnabled)
		{
			return false;
		}
	}
	
	printf("Listening to change notifications on path %ws\n\n", finalPath);
	unsigned char buffer[8192];

	ZeroMemory(&overlapped, sizeof(overlapped));
	overlapped.hEvent = CreateEvent(NULL, /*manual reset*/ TRUE, /*initial state*/ FALSE, NULL);
	if (overlapped.hEvent == NULL)
	{
		*reason = FormattedString("Create event failed with code %d \n\n", GetLastError());
		CloseHandle(directory);
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

	
	CreateAndDeleteFile(L"D:\\home\\site\\wwwroot\\TestDirectories\\StandardRDC\\newfile123.txt");

	Sleep(100); //sleep to handle delete compensation

	DWORD dwWaitStatus = WaitForSingleObject(overlapped.hEvent, 1000*60);

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

bool TestReadDirectoryChangesInDetour(std::string* reason)
/*
	Test read directory changes all in dynamic cache detour
*/
{
	enterDetour();
	//open directory handle to listen to change notifications on
	HANDLE directory = CreateFileW(L"D:\\home\\site\\wwwroot\\TestDirectories\\RDCinDetour",
		FILE_LIST_DIRECTORY,
		FILE_SHARE_READ,
		NULL,
		OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
		NULL);

	if (directory == INVALID_HANDLE_VALUE)
	{
		*reason = FormattedString("CreatefileW (directory) call failed with code %d", GetLastError());
		leaveDetour();
		return false;
	}

	leaveDetour();
	WCHAR finalPath[MAX_PATH] = L"";
	DWORD dwRet;
	OVERLAPPED overlapped = { 0 };
	DWORD dwBytesTransferred = 0;
	DWORD dwBytesReturned = 0;
	
	dwRet = EnterDetourAndGetFinalPathNameByHandle(
		directory,
		finalPath,
		_countof(finalPath));

	enterDetour();
	if (dwRet == 0)
	{
		*reason = FormattedString("GetFinalPathNameByHandle failed with code %d\n\n", GetLastError());
		CloseHandle(directory);
		leaveDetour();
		return false;
	}

	if (IsDynamicCachePath(finalPath))
	{
		*reason = FormattedString("Expected remote path, but instead listening for change notifications on a local path. Path is %ws", finalPath);
		CloseHandle(directory);
		leaveDetour();
		return false;
	}

	printf("Listening to change notifications on path %ws\n\n", finalPath);
	unsigned char buffer[8192];

	ZeroMemory(&overlapped, sizeof(overlapped));
	overlapped.hEvent = CreateEvent(NULL, /*manual reset*/ TRUE, /*initial state*/ FALSE, NULL);
	if (overlapped.hEvent == NULL)
	{
		*reason = FormattedString("Create event failed with code %d \n\n", GetLastError());
		CloseHandle(directory);
		leaveDetour();
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

	//Create a new file to trigger a change notification
	CreateAndDeleteFile(L"D:\\home\\site\\wwwroot\\TestDirectories\\RDCinDetour\\newfile.txt");

	DWORD dwWaitStatus = WaitForSingleObject(overlapped.hEvent, 1000 * 60);

	if (dwWaitStatus == WAIT_OBJECT_0)
	{
		dwRet = GetOverlappedResult(directory, &overlapped, &dwBytesTransferred, FALSE);

		if (!dwRet)
		{
			*reason = FormattedString("GetOverlappedResult failed with code %d\n\n", GetLastError());
			CloseHandle(directory);
			leaveDetour();
			return false;
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

			CloseHandle(overlapped.hEvent);
			CloseHandle(directory);
			leaveDetour();
			return TRUE;
		}
	}
	else
	{
		//assume timeout
		*reason = "ReadDirectoryChanges() call timed out";
		CloseHandle(overlapped.hEvent);
		CloseHandle(directory);
		leaveDetour();
		return FALSE;
	}

	leaveDetour();
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

	Note: I might need to just test this manually. when I integrate into the main I will be able to pre-create the directories.

*/
{
	BOOL result = FALSE;
	DWORD dwWaitStatus;
	WCHAR finalPath[MAX_PATH] = L"";
	OVERLAPPED overlapped = { 0 };
	DWORD dwBytesTransferred = 0;
	DWORD dwBytesReturned = 0;

	if (verboseLoggingEnabled)
	{
		printf("Marking directory as recently changed D:\\home\\site\\wwwroot\\TestDirectories\\a \n\n");
	}
	enterDetour();
	//Create a new directory very nested under wwwroot
	//Note: this is not pretty code...
	DWORD dwRet = CreateDirectoryW(L"D:\\home\\site\\wwwroot\\TestDirectories\\a", NULL) &&
				  CreateDirectoryW(L"D:\\home\\site\\wwwroot\\TestDirectories\\a\\b", NULL) &&
				  CreateDirectoryW(L"D:\\home\\site\\wwwroot\\TestDirectories\\a\\b\\c", NULL) &&
				  CreateDirectoryW(L"D:\\home\\site\\wwwroot\\TestDirectories\\a\\b\\c\\d", NULL);


	//brief sleep to prevent change notifiation firing
	Sleep(100);
	
	leaveDetour();
	if (!dwRet)
	{
		*reason = FormattedString("Create new directory failed with %d", GetLastError());
		return result;
	}

	//open directory handle to listen to change notifications on
	HANDLE directory = CreateFileW(L"D:\\home\\site\\wwwroot\\TestDirectories\\a",
		FILE_LIST_DIRECTORY,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL,
		OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
		NULL);

	if (directory == INVALID_HANDLE_VALUE)
	{
		*reason = FormattedString("CreatefileW (directory) call failed with code %d \n\n", GetLastError());
		goto Finished;
	}

	dwRet = EnterDetourAndGetFinalPathNameByHandle(
		directory,
		finalPath,
		_countof(finalPath));

	if (dwRet == 0)
	{
		*reason = FormattedString("GetFinalPathNameByHandle failed with code %d \n\n", GetLastError());
		goto Finished;
	}

	if (!IsDynamicCachePath(finalPath))
	{
		*reason = FormattedString("Expected local path, but instead listening for change notifications on a remote path. Path is %ws \n", finalPath);
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

	CreateAndDeleteFile(L"D:\\home\\site\\wwwroot\\TestDirectories\\a\\b\\c\\d\\newfile.txt");
	
	dwWaitStatus = WaitForSingleObject(overlapped.hEvent, 1000 * 60);

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

	dwRet = RemoveDirectory(L"D:\\home\\site\\wwwroot\\TestDirectories\\a\\b\\c\\d") &&
			RemoveDirectory(L"D:\\home\\site\\wwwroot\\TestDirectories\\a\\b\\c") &&
			RemoveDirectory(L"D:\\home\\site\\wwwroot\\TestDirectories\\a\\b") &&
			RemoveDirectory(L"D:\\home\\site\\wwwroot\\TestDirectories\\a");

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

	dwRet = CreateDirectoryW(L"D:\\home\\site\\wwwroot\\TestDirectories\\newDir", NULL);
	
	if (!dwRet)
	{
		*reason = FormattedString("Directory creation failed with code %d", GetLastError());
		return false;
	}

	//Open the directory handle
	HANDLE directory = CreateFileW(L"D:\\home\\site\\wwwroot\\TestDirectories\\newDir",
		GENERIC_READ,
		FILE_SHARE_READ,
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
		finalPath,
		_countof(finalPath));

	CloseHandle(directory);

	if (dwRet == 0)
	{
		*reason = FormattedString("GetFinalPathNameByHandle failed with code %d\n\n", GetLastError());
		return false;
	}

	if (!IsDynamicCachePath(finalPath))
	{
		*reason = FormattedString("Newly created directory handle is a remote handle %ws", finalPath);
		RemoveDirectory(L"D:\\home\\site\\wwwroot\\TestDirectories\\newDir");
		return false;
	}
	
	//now delete the directory and ensure it is cleaned up properly
	dwRet = RemoveDirectory(L"D:\\home\\site\\wwwroot\\TestDirectories\\newDir");
	if (!dwRet)
	{
		*reason = FormattedString("Directory deletion failed with code %d", GetLastError());
		return false;
	}

	//try to open the directory handle remotely and ensure this operation fails. The GENERIC_WRITE flag will ensure it will open remotely
	directory = CreateFileW(L"D:\\home\\site\\wwwroot\\TestDirectories\\newDir",
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL,
		OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS,
		NULL);

	if (directory == INVALID_HANDLE_VALUE)
	{
		return true;
	}
	else
	{
		*reason = FormattedString("Directory unexpectedly exists remotely when it should have been deleted", GetLastError());
		CloseHandle(directory);
		return false;
	}

	return false;
}

bool TestWriteDirectoryAttributes(std::string* reason)
{
	//create a directory and mark it as old
	BOOL dwRet = CreateDirectoryW(L"D:\\home\\site\\wwwroot\\TestDirectories\\oldDir", NULL);

	if (!dwRet)
	{
		*reason = FormattedString("Directory creation failed with code %d", GetLastError());
		return false;
	}

	//Open the directory handle
	HANDLE directory = CreateFileW(L"D:\\home\\site\\wwwroot\\TestDirectories\\oldDir",
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_WRITE_ATTRIBUTES,
		NULL,
		OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS,
		NULL);

	WCHAR finalPath[MAX_PATH] = L"";

	dwRet = EnterDetourAndGetFinalPathNameByHandle(
		directory,
		finalPath,
		_countof(finalPath));

	if (dwRet == 0)
	{
		*reason = FormattedString("GetFinalPathNameByHandle failed with code %d\n\n", GetLastError());
		CloseHandle(directory);
		RemoveDirectoryW(L"D:\\home\\site\\wwwroot\\TestDirectories\\oldDir");
		return false;
	}

	if (IsDynamicCachePath(finalPath))
	{
		*reason = FormattedString("Path is a dynamic cache when it should be a remote path %ss\n\n", finalPath);
		CloseHandle(directory);
		RemoveDirectoryW(L"D:\\home\\site\\wwwroot\\TestDirectories\\oldDir");
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
		RemoveDirectoryW(L"D:\\home\\site\\wwwroot\\TestDirectories\\oldDir");
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
		RemoveDirectoryW(L"D:\\home\\site\\wwwroot\\TestDirectories\\oldDir");
		return false;
	}

	CloseHandle(directory);
	RemoveDirectoryW(L"D:\\home\\site\\wwwroot\\TestDirectories\\oldDir");
	return true;
}

bool TestRenameDirectory(std::string* reason)
{
	//Need to create a directory to rename
	BOOL dwRet = CreateDirectoryW(L"D:\\home\\site\\wwwroot\\TestDirectories\\directoryToRename", NULL);
	
	if (!dwRet)
	{
		*reason = FormattedString("Directory creation failed with code %d", GetLastError());
		return false;
	}
	
	//Rename the directory
	dwRet = MoveFileExA(
				"D:\\home\\site\\wwwroot\\TestDirectories\\directoryToRename",
				"D:\\home\\site\\wwwroot\\TestDirectories\\RenamedDirectory",
				NULL);

	if (!dwRet)
	{
		*reason = FormattedString("Rename directory failed with code %d \n\n", GetLastError());
	}

	//ensure it exists remotely by detouring dynamic cache before opening
	enterDetour();

	HANDLE hRenamedDir = CreateFileW(L"D:\\home\\site\\wwwroot\\TestDirectories\\RenamedDirectory",
								GENERIC_READ,
								FILE_SHARE_READ,
								NULL,
								OPEN_EXISTING,
								FILE_FLAG_BACKUP_SEMANTICS,
								NULL);
	leaveDetour();

	WCHAR finalPath[MAX_PATH] = L"";

	dwRet = EnterDetourAndGetFinalPathNameByHandle(
		hRenamedDir,
		finalPath,
		_countof(finalPath));

	if (dwRet == 0)
	{
		*reason = FormattedString("GetFinalPathNameByHandle failed with code %ds\n\n", GetLastError());
		CloseHandle(hRenamedDir);
		return false;
	}

	bool  isCachePath = IsDynamicCachePath(finalPath);

	if (isCachePath)
	{
		*reason = FormattedString("Expected a remote path but %ws is a local path \n\n", finalPath);
		CloseHandle(hRenamedDir);
		RemoveDirectoryW(L"D:\\home\\site\\wwwroot\\TestDirectories\\RenamedDirectory");
		return false;
	}

	//need to ensure cache is hydrated though
	CloseHandle(hRenamedDir);
	RemoveDirectoryW(L"D:\\home\\site\\wwwroot\\TestDirectories\\RenamedDirectory");
	return true;
}

bool TestReadDirectoryChangesWwwroot(std::string* reason)
/*+
	Test ReadDirectoryChanges call on wwwroot since this may behave slightly differently than nested subdirectories.
	I am not entering dynamic cache detour so this should behave like a customer appliation

	This should be the last test to run in the test suite since I am triggering a change notification on wwwroot
*/
{
	//Add a sleep to prevent other change notifications from firing
	Sleep(100);

	HANDLE directory = CreateFileW(L"D:\\home\\site\\wwwroot",
		FILE_LIST_DIRECTORY,
		FILE_SHARE_READ,
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
	OVERLAPPED overlapped = { 0 };
	DWORD dwBytesTransferred = 0;
	DWORD dwBytesReturned = 0;

	dwRet = EnterDetourAndGetFinalPathNameByHandle(
		directory,
		finalPath,
		_countof(finalPath));

	if (dwRet == 0)
	{
		*reason = FormattedString("GetFinalPathNameByHandle failed with code %d\n\n", GetLastError());
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
		CloseHandle(directory);
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

	CreateAndDeleteFile(L"D:\\home\\site\\wwwroot\\newfile123.txt");

	DWORD dwWaitStatus = WaitForSingleObject(overlapped.hEvent, 1000 * 60);

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

		WCHAR filename[MAX_PATH] = { 0 };

		DWORD action = notifyInfo->Action;

		// Copy out the file name as it is not null terminated.
		if (notifyInfo->FileNameLength < (MAX_PATH * sizeof(WCHAR)))
		{
			CopyMemory(filename, notifyInfo->FileName, notifyInfo->FileNameLength);
			printf("Change notification arrived for file %ws and action %d\n\n", filename, action);
			CloseHandle(overlapped.hEvent);
			CloseHandle(directory);
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

bool TestReadDirectoryChangesWwwrootInDetour(std::string* reason)
/*
	Tests change notifications in wwwroot while in dynamic cache detour
*/
{
	//Add a sleep to prevent other change notifications from firing
	Sleep(100);
	
	WCHAR finalPath[MAX_PATH] = L"";
	DWORD dwRet;
	OVERLAPPED overlapped = { 0 };
	DWORD dwBytesTransferred = 0;
	DWORD dwBytesReturned = 0;
	BOOL result = false;

	enterDetour();

	HANDLE directory = CreateFileW(L"D:\\home\\site\\wwwroot",
		FILE_LIST_DIRECTORY,
		FILE_SHARE_READ,
		NULL,
		OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
		NULL);

	leaveDetour();
	if (directory == INVALID_HANDLE_VALUE)
	{
		*reason = FormattedString("CreatefileW (directory) call failed with code %d", GetLastError());
		return false;
	}

	dwRet = EnterDetourAndGetFinalPathNameByHandle(
		directory,
		finalPath,
		_countof(finalPath));

	enterDetour();
	if (dwRet == 0)
	{
		*reason = FormattedString("GetFinalPathNameByHandle failed with code %d\n\n", GetLastError());
		CloseHandle(directory);
		leaveDetour();
		return false;
	}

	if (IsDynamicCachePath(finalPath))
	{
		*reason = FormattedString("Expected remote path, but instead listening for change notifications on a local path. Path is %ws", finalPath);
		CloseHandle(directory);
		leaveDetour();
		return false;
	}

	printf("Listening to change notifications on path %ws\n\n", finalPath);
	unsigned char buffer[8192];

	ZeroMemory(&overlapped, sizeof(overlapped));
	overlapped.hEvent = CreateEvent(NULL, /*manual reset*/ TRUE, /*initial state*/ FALSE, NULL);
	if (overlapped.hEvent == NULL)
	{
		*reason = FormattedString("Create event failed with code %d \n\n", GetLastError());
		CloseHandle(directory);
		leaveDetour();
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

	CreateAndDeleteFile(L"D:\\home\\site\\wwwroot\\newfile123.txt");

	DWORD dwWaitStatus = WaitForSingleObject(overlapped.hEvent, 1000 * 60);

	if (dwWaitStatus == WAIT_OBJECT_0)
	{
		dwRet = GetOverlappedResult(directory, &overlapped, &dwBytesTransferred, FALSE);

		if (!dwRet)
		{
			*reason = FormattedString("GetOverlappedResult failed with code %d\n\n", GetLastError());
			CloseHandle(directory);
			leaveDetour();
			return false;
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
			CloseHandle(overlapped.hEvent);
			CloseHandle(directory);
			leaveDetour();
			return TRUE;
		}
	}
	else
	{
		//assume timeout
		*reason = "ReadDirectoryChanges() call timed out";
		CloseHandle(overlapped.hEvent);
		CloseHandle(directory);
		//leaveDetour();
		return FALSE;
	}

	return FALSE;
}

bool TestCancelIOFile(std::string* reason)
/*
	Tests DetourNtCancelIoFile.

	Need to call on a handle which has been recently changes and used to call into FindFirstFile. This will ensure the hande is in the map. 
*/
{
	bool result = FALSE;
	HANDLE hFind;
	HANDLE file2 = INVALID_HANDLE_VALUE;
	HANDLE dir = INVALID_HANDLE_VALUE;
	DWORD hr;

	enterDetour();
	//Create a new file which should not be cached
	HANDLE file = CreateFileW(L"D:\\home\\site\\wwwroot\\TestDirectories\\CancelIo\\newfile1.txt",
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

	//Mark the directory as recently changed by creating and deleting a file
	file2 = CreateFileW(L"D:\\home\\site\\wwwroot\\TestDirectories\\CancelIo\\newfile2.txt",
		GENERIC_WRITE | DELETE,
		FILE_SHARE_DELETE,
		NULL,
		OPEN_ALWAYS,
		FILE_FLAG_DELETE_ON_CLOSE,
		NULL);

	CloseHandle(file2);

	//Call findfirst on file with .txt 
	WIN32_FIND_DATAA FindFileData;
	hFind = FindFirstFileA("D:\\home\\site\\wwwroot\\TestDirectories\\CancelIo\\*.txt", &FindFileData);

	if (hFind == INVALID_HANDLE_VALUE)
	{
		*reason = FormattedString("FindFirstFile failed (%d)\n", GetLastError());
		goto Finished;
	}

	//Call into CancelIo with the directory handle
	dir = CreateFileW(L"D:\\home\\site\\wwwroot\\TestDirectories\\CancelIo",
		GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL,
		OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS,
		NULL);

	hr = CancelIoEx(dir, NULL);

	if (FAILED(hr))
	{
		*reason = FormattedString("CancelIoEx failed (%d)\n", GetLastError());
		FindClose(hFind);
		goto Finished;
	}

	hr = FindClose(hFind);
	if (!hr)
	{
		printf("FindClose failed (%d)\n", GetLastError());
	}

	CloseHandle(dir);
	result = TRUE;

Finished:

	if (dir != INVALID_HANDLE_VALUE)
	{
		CloseHandle(dir);
	}

	CloseHandle(file);
	DeleteFileW(L"D:\\home\\site\\wwwroot\\TestDirectories\\CancelIo\\newfile1.txt");
	return result;
}

bool TestReadDirectoryChangesHomeSite(std::string* reason)
/*
	D:\home\site is scoped within dynamic cache but I have been observing some change notifications going remote
	so making a special test case just for this
*/
{
	Sleep(100);

	HANDLE directory = CreateFileW(L"D:\\home\\site",
		FILE_LIST_DIRECTORY,
		FILE_SHARE_READ,
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
	OVERLAPPED overlapped = { 0 };
	DWORD dwBytesTransferred = 0;
	DWORD dwBytesReturned = 0;

	dwRet = EnterDetourAndGetFinalPathNameByHandle(
		directory,
		finalPath,
		_countof(finalPath));

	if (dwRet == 0)
	{
		*reason = FormattedString("GetFinalPathNameByHandle failed with code %d\n\n", GetLastError());
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
		CloseHandle(directory);
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

	CreateAndDeleteFile(L"D:\\home\\site\\newfile123.txt");

	DWORD dwWaitStatus = WaitForSingleObject(overlapped.hEvent, 1000 * 60);

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

		WCHAR filename[MAX_PATH] = { 0 };

		DWORD action = notifyInfo->Action;

		// Copy out the file name as it is not null terminated.
		if (notifyInfo->FileNameLength < (MAX_PATH * sizeof(WCHAR)))
		{
			CopyMemory(filename, notifyInfo->FileName, notifyInfo->FileNameLength);
			printf("Change notification arrived for file %ws and action %d\n\n", filename, action);
			CloseHandle(overlapped.hEvent);
			CloseHandle(directory);
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

bool OverwhelmChangeNotifications(std::string* reason)
{
	for (int i = 0; i < 6; i++)
	{
		CreateManyFiles();
	}

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
	
	//crete resources to be used later
	//CreateDirectory(L"D:\\home\\site\\wwwroot\\directoryToRename", NULL);

	//TODO: delete any files/directories if they exist from previous runs
	//It's okay if these fail -- they might not exist so failure would be expected.
	//RemoveDirectoryW(L"D:\\home\\site\\wwwroot\\oldDir");
	//RemoveDirectoryW(L"D:\\home\\site\\wwwroot\\RenamedDirectory");

	return true;
}

void SetupTestEnvironment()
{
	BOOL dwRet = CreateDirectory(L"D:\\home\\site\\wwwroot\\TestDirectories", NULL) &&
		CreateDirectory(L"D:\\home\\site\\wwwroot\\TestDirectories\\FindFirst", NULL) &&
		CreateDirectory(L"D:\\home\\site\\wwwroot\\TestDirectories\\FindFirstHydrated", NULL) &&
		CreateDirectory(L"D:\\home\\site\\wwwroot\\TestDirectories\\TestOpenDirRecentlyChanged1", NULL) &&
		CreateDirectory(L"D:\\home\\site\\wwwroot\\TestDirectories\\TestOpenDirRecentlyChanged2", NULL) &&
		CreateDirectory(L"D:\\home\\site\\wwwroot\\TestDirectories\\StandardRDC", NULL) &&
		CreateDirectory(L"D:\\home\\site\\wwwroot\\TestDirectories\\DirectoryOpenForWrite", NULL) &&
		CreateDirectory(L"D:\\home\\site\\wwwroot\\TestDirectories\\UnusedDirectory", NULL) &&
		CreateDirectory(L"D:\\home\\site\\wwwroot\\TestDirectories\\RDCinDetour", NULL) &&
		CreateDirectory(L"D:\\home\\site\\wwwroot\\TestDirectories\\CancelIo", NULL);

	if (!dwRet)
	{
		printf("Error setting up environment for tests. Failed with code %d", GetLastError());
	}

	HANDLE file = CreateFileW(L"D:\\home\\site\\wwwroot\\TestDirectories\\FindFirstHydrated\\newfile.txt",
		GENERIC_WRITE,
		0,
		NULL,
		OPEN_ALWAYS,
		0,
		NULL);

	CloseHandle(file);

	printf("Environment setup complete. Please rerun tests now with environment setup flag set to false.");
}

int main()
{
	if (oneTimeEnvSetupForTests)
	{
		printf("Setting up test environment... \n\n");
		fflush(stdout);
		SetupTestEnvironment();

		//return immediately since directories have been recently created
		return 0;
	}

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

	/*
	ADD_TEST_CASE(TestReadDirectoryChangesNoDetour);
	ADD_TEST_CASE(TestOpenDirectoryNotRecentlyChanged);
	ADD_TEST_CASE(TestOpenRecentlyChangeDirHandleAddFile);
	ADD_TEST_CASE(TestOpenRecentlyChangedDirectoryDeleteFile);
	ADD_TEST_CASE(TestQueryDirectoryFileOnRecentlyChangedDir);
	ADD_TEST_CASE(TestQueryDirectoryFileOnHydratedDir);
	ADD_TEST_CASE(TestOpenDirectoryWithWrite);
	ADD_TEST_CASE(TestReadDirectoryChangesNestedDirectory);
	ADD_TEST_CASE(TestReadDirectoryChangesInDetour);
	ADD_TEST_CASE(TestWriteDirectoryAttributes);
	ADD_TEST_CASE(TestRenameDirectory);
	
	//these two test cases should be the last to run since I am triggering a change notification in wwwroot
	ADD_TEST_CASE(TestReadDirectoryChangesWwwroot);
	ADD_TEST_CASE(TestReadDirectoryChangesWwwrootInDetour);


	ADD_TEST_CASE(TestReadDirectoryChangesHomeSite);
	*/

	ADD_TEST_CASE(OverwhelmChangeNotifications);

	RunTests();
}

#pragma endregion