#include <windows.h>
#include <assert.h>

#ifndef UNICODE
#define UNICODE
#endif

#ifndef _UNICODE
#define _UNICODE
#endif

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParameter, LPARAM lParameter) {
	return DefWindowProc(window, message, wParameter, lParameter);
}

// Create a window!
int WINAPI wWinMain(HINSTANCE appInstance, HINSTANCE previousInstance, PWSTR commandLine, int visibility) {
	int errorCode;
	WNDCLASS windowClass = { 0 };
	windowClass.hInstance = appInstance;
	windowClass.lpszClassName = L"Window Class";
	windowClass.lpfnWndProc = WindowProcedure;
	RegisterClass(&windowClass);

	CreateWindow(windowClass.lpszClassName, L"Window", WS_OVERLAPPEDWINDOW,
				 CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
				 NULL, NULL, appInstance, NULL);
	errorCode = GetLastError();
	return 0;
}