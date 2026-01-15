#include <windows.h>
#include <assert.h>

#ifndef UNICODE
#define UNICODE
#endif

#ifndef _UNICODE
#define _UNICODE
#endif

int WINAPI wWinMain(HINSTANCE appInstance, HINSTANCE previousInstance, PWSTR commandLine, int visibility) {
	int selection = MessageBox(NULL, L"Hello", L"Message", MB_OK);
	return 0;
}