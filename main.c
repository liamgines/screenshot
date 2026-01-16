#include <windows.h>
#include <assert.h>
#include <stdint.h>

#ifndef UNICODE
#define UNICODE
#endif

#ifndef _UNICODE
#define _UNICODE
#endif

#define SCREEN_HANDLE NULL
#define SCREEN_AREA (SCREEN_WIDTH * SCREEN_HEIGHT)

static uint32_t *screenPixels = NULL;
static BITMAPINFO info = { 0 };
static int SCREEN_WIDTH = 0;
static int SCREEN_HEIGHT = 0;

int HandleKeyUp(HWND window, UINT message, WPARAM wParameter, LPARAM lParameter) {
	switch (wParameter) {
		case VK_ESCAPE:
			DestroyWindow(window);
			return 0;

		default:
			return DefWindowProc(window, message, wParameter, lParameter);
	}
}

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParameter, LPARAM lParameter) {
	switch (message) {
		case WM_KEYUP:
			return HandleKeyUp(window, message, wParameter, lParameter);

		case WM_PAINT:
			PAINTSTRUCT paint;
			HDC client = BeginPaint(window, &paint);
			int scanLinesSet = SetDIBitsToDevice(client, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 0, 0, 0, SCREEN_HEIGHT, screenPixels, &info, DIB_RGB_COLORS);
			EndPaint(window, &paint);
			return 0;

		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;

		default:
			return DefWindowProc(window, message, wParameter, lParameter);
	}
}

int WINAPI wWinMain(HINSTANCE appInstance, HINSTANCE previousInstance, PWSTR commandLine, int visibility) {
	SCREEN_WIDTH = GetSystemMetrics(SM_CXSCREEN);
	SCREEN_HEIGHT = GetSystemMetrics(SM_CYSCREEN);

	HDC screen = GetDC(SCREEN_HANDLE);
	HDC memory = CreateCompatibleDC(screen);

	// Create screen compatible bitmap and associate it with the memory device context
	HBITMAP memoryBitmap = CreateCompatibleBitmap(screen, SCREEN_WIDTH, SCREEN_HEIGHT);
	HBITMAP previousMemoryBitmap = SelectObject(memory, memoryBitmap);

	// Transfer color data from screen to memory
	BOOL transferred = BitBlt(memory, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, screen, 0, 0, SRCCOPY);

	// Specify format for the bitmap data to be returned
	BITMAPINFOHEADER header = { 0 };
	header.biSize = sizeof(header);
	header.biWidth = SCREEN_WIDTH;
	header.biHeight = -SCREEN_HEIGHT;
	header.biPlanes = 1;
	header.biBitCount = 32;
	header.biCompression = BI_RGB;
	info.bmiHeader = header;

	// Capture instance of screen pixels
	screenPixels = malloc(sizeof(uint32_t) * SCREEN_AREA);
	int scanLinesCopied = GetDIBits(memory, memoryBitmap, 0, SCREEN_HEIGHT, screenPixels, &info, DIB_RGB_COLORS);

	// Clean up
	ReleaseDC(SCREEN_HANDLE, screen);
	assert(SelectObject(memory, previousMemoryBitmap) == memoryBitmap);
	DeleteDC(memory);
	DeleteObject(memoryBitmap);

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

	free(screenPixels);

	return 0;
}