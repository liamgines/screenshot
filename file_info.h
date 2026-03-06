#ifndef FILE_INFO_H
#define FILE_INFO_H

#include <windows.h>

#define CONFIG_FILE L"screenshot.ini"

// https://stackoverflow.com/a/6218957
static BOOL FileOrDirectoryExists(LPCWSTR path) {
	DWORD attributes = GetFileAttributes(path);
	return attributes != INVALID_FILE_ATTRIBUTES;
}

// https://stackoverflow.com/a/6218445/32242805
static BOOL DirectoryExists(LPCWSTR path) {
	DWORD attributes = GetFileAttributes(path);
	return (attributes != INVALID_FILE_ATTRIBUTES) && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static BOOL GetExeDirectory(wchar_t *destination) {
	GetModuleFileName(NULL, destination, MAX_PATH);
	if (GetLastError()) return FALSE;
	return (PathCchRemoveFileSpec(destination, MAX_PATH) == S_OK);
}

static BOOL GetConfigPath(wchar_t *destination) {
	wchar_t exeDirectory[MAX_PATH];
	return GetExeDirectory(exeDirectory) && (PathCombine(destination, exeDirectory, CONFIG_FILE) != NULL);
}

#endif // FILE_INFO_H