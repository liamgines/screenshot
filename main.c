#include <windows.h>
#include <assert.h>

#ifndef UNICODE
#define UNICODE
#endif

#ifndef _UNICODE
#define _UNICODE
#endif

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParameter, LPARAM lParameter) {
	switch (message) {
		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;

		default:
			return DefWindowProc(window, message, wParameter, lParameter);
	}
}

// Create a window!
int WINAPI wWinMain(HINSTANCE appInstance, HINSTANCE previousInstance, PWSTR commandLine, int visibility) {
	int errorCode;
	WNDCLASS windowClass = { 0 };
	windowClass.hInstance = appInstance;
	windowClass.lpszClassName = L"Window Class";
	windowClass.lpfnWndProc = WindowProcedure;
	RegisterClass(&windowClass);

	HWND window = CreateWindow(windowClass.lpszClassName, L"Window", WS_OVERLAPPEDWINDOW,
				 CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
				 NULL, NULL, appInstance, NULL);

	ShowWindow(window, visibility);
	MSG message;
	BOOL messageReturned;
	while ((messageReturned = GetMessage(&message, window, 0, 0)) != 0) {
		if (messageReturned == -1) {
			// TODO: Handle error and possibly exit
			break;
		}
		TranslateMessage(&message);
		DispatchMessage(&message);
	}
	errorCode = GetLastError();
	return 0;
}