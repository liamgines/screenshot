#include <windows.h>
#include <assert.h>
#include <windowsX.h>

#define SWAP(TYPE, x, y) \
do {					 \
	TYPE temp;			 \
	temp = x;			 \
	x = y;				 \
	y = temp;			 \
} while (0)

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#define SCREEN_HANDLE NULL
#define SCREEN_AREA (SCREEN_WIDTH * SCREEN_HEIGHT)

static int SCREEN_WIDTH = 0;
static int SCREEN_HEIGHT = 0;
static RECT screenRectangle = { 0 };
static RECT selectionRectangle = { 0 };
static HDC memory;

RECT GetNormalizedRectangle(RECT rectangle) {
	if (rectangle.right - rectangle.left < 0) SWAP(LONG, rectangle.right, rectangle.left);
	if (rectangle.bottom - rectangle.top < 0) SWAP(LONG, rectangle.bottom, rectangle.top);
	return rectangle;
}

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

		case WM_LBUTTONDOWN:
			selectionRectangle.left = GET_X_LPARAM(lParameter);
			selectionRectangle.top = GET_Y_LPARAM(lParameter);
			selectionRectangle.right = selectionRectangle.left;
			selectionRectangle.bottom = selectionRectangle.top;
			BOOL repaint = InvalidateRect(window, NULL, TRUE);
			return 0;

		case WM_MOUSEMOVE:
			if (wParameter == MK_LBUTTON) {
				selectionRectangle.right = GET_X_LPARAM(lParameter);
				selectionRectangle.bottom = GET_Y_LPARAM(lParameter);
				BOOL repaint = InvalidateRect(window, NULL, TRUE);
			}
			return 0;

		case WM_PAINT:
			PAINTSTRUCT paint;
			HDC client = BeginPaint(window, &paint);

			HDC scene = CreateCompatibleDC(client);
			HBITMAP sceneBitmap = CreateCompatibleBitmap(client, SCREEN_WIDTH, SCREEN_HEIGHT);
			HBITMAP previousSceneBitmap = SelectObject(scene, sceneBitmap);
			HBRUSH backgroundColor = CreateSolidBrush(RGB(0, 0, 0));
			assert(FillRect(scene, &screenRectangle, backgroundColor));
			DeleteObject(backgroundColor);
			BLENDFUNCTION blend = { 0 };
			blend.BlendOp = AC_SRC_OVER;
			blend.SourceConstantAlpha = 128;
			blend.AlphaFormat = AC_SRC_ALPHA;
			BOOL blended = GdiAlphaBlend(scene, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, memory, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, blend);

			RECT displayRectangle = GetNormalizedRectangle(selectionRectangle);
			BitBlt(scene, displayRectangle.left, displayRectangle.top, displayRectangle.right - displayRectangle.left, displayRectangle.bottom - displayRectangle.top,
				   memory, displayRectangle.left, displayRectangle.top, SRCCOPY);

			BitBlt(client, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, scene, 0, 0, SRCCOPY);

			assert(SelectObject(scene, previousSceneBitmap) == sceneBitmap);
			DeleteDC(scene);
			DeleteObject(sceneBitmap);
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

	screenRectangle.left = 0;
	screenRectangle.top = 0;
	// "By convention, the right and bottom edges of the rectangle are normally considered exclusive."
	screenRectangle.right = SCREEN_WIDTH;
	screenRectangle.bottom = SCREEN_HEIGHT;

	HDC screen = GetDC(SCREEN_HANDLE);
	memory = CreateCompatibleDC(screen);

	// Create screen compatible bitmap and associate it with the memory device context
	HBITMAP memoryBitmap = CreateCompatibleBitmap(screen, SCREEN_WIDTH, SCREEN_HEIGHT);
	HBITMAP previousMemoryBitmap = SelectObject(memory, memoryBitmap);

	// Transfer color data from screen to memory
	BOOL transferred = BitBlt(memory, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, screen, 0, 0, SRCCOPY);

	// Clean up
	ReleaseDC(SCREEN_HANDLE, screen);

	WNDCLASS windowClass = { 0 };
	windowClass.hInstance = appInstance;
	windowClass.lpszClassName = L"Window Class";
	windowClass.lpfnWndProc = WindowProcedure;
	RegisterClass(&windowClass);

	HWND window = CreateWindow(windowClass.lpszClassName, L"", WS_POPUP,
				 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT,
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

	// Clean up
	assert(SelectObject(memory, previousMemoryBitmap) == memoryBitmap);
	DeleteDC(memory);
	DeleteObject(memoryBitmap);

	return 0;
}