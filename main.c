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

#define MIDPOINT(x, y) (((x) + (y)) / 2)

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#define SCREEN_HANDLE NULL
#define SCREEN_AREA (SCREEN_WIDTH * SCREEN_HEIGHT)
#define BOX_SIZE 6

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

POINT GetPoint(LPARAM lParameter) {
	POINT point = { .x = GET_X_LPARAM(lParameter), .y = GET_Y_LPARAM(lParameter) };
	return point;
}

POINT GetDifference(POINT p1, POINT p2) {
	POINT difference;
	difference.x = p1.x - p2.x;
	difference.y = p1.y - p2.y;
	return difference;
}

RECT TranslateRectangle(RECT rectangle, POINT translation) {
	rectangle.left += translation.x;
	rectangle.top += translation.y;
	rectangle.right += translation.x;
	rectangle.bottom += translation.y;
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

RECT GetBox(POINT p) {
	RECT box = { .left = p.x - BOX_SIZE/2, .top = p.y - BOX_SIZE/2, .right = p.x + BOX_SIZE/2, .bottom = p.y + BOX_SIZE/2};
	return box;
}

void PaintAnchor(HDC destination, POINT p) {
	HBRUSH boxColor = CreateSolidBrush(RGB(0, 255, 0));
	RECT box = GetBox(p);
	FillRect(destination, &box, boxColor);
	DeleteObject(boxColor);
}

LONG GetArea(RECT r) {
	LONG w = r.right - r.left;
	LONG h = r.bottom - r.top;
	return w * h;
}

BOOL HasArea(RECT r) {
	if (GetArea(r) != 0) return TRUE;
	return FALSE;
}

typedef struct {
	POINT topLeft;
	POINT topMid;
	POINT topRight;

	POINT midLeft;
	POINT midRight;

	POINT bottomLeft;
	POINT bottomMid;
	POINT bottomRight;
} Anchors;

Anchors GetAnchors(RECT r) {
	Anchors anchors;

	anchors.topLeft.x = r.left;
	anchors.topLeft.y = r.top;

	anchors.topMid.x = MIDPOINT(r.left, r.right);
	anchors.topMid.y = r.top;

	anchors.topRight.x = r.right;
	anchors.topRight.y = r.top;

	anchors.midLeft.x = r.left;
	anchors.midLeft.y = MIDPOINT(r.top, r.bottom);

	anchors.midRight.x = r.right;
	anchors.midRight.y = MIDPOINT(r.top, r.bottom);

	anchors.bottomLeft.x = r.left;
	anchors.bottomLeft.y = r.bottom;

	anchors.bottomMid.x = MIDPOINT(r.left, r.right);
	anchors.bottomMid.y = r.bottom;

	anchors.bottomRight.x = r.right;
	anchors.bottomRight.y = r.bottom;

	return anchors;
}

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParameter, LPARAM lParameter) {
	static BOOL drag = FALSE;
	static POINT previousPosition;
	static LONG *selectedXCorner = NULL;
	static LONG *selectedYCorner = NULL;
	RECT displayRectangle = GetNormalizedRectangle(selectionRectangle);
	Anchors anchors = GetAnchors(displayRectangle);

	switch (message) {
		case WM_KEYUP:
			return HandleKeyUp(window, message, wParameter, lParameter);

		case WM_LBUTTONDOWN: {
			POINT point = GetPoint(lParameter);
			BOOL cursorInSelection = PtInRect(&displayRectangle, point);

			RECT topLeft = GetBox(anchors.topLeft);
			if (PtInRect(&topLeft, point)) {
				selectedXCorner = &selectionRectangle.left;
				selectedYCorner = &selectionRectangle.top;
			}
			// Reset selection
			else if (!cursorInSelection) {
				selectionRectangle.left = point.x;
				selectionRectangle.top = point.y;
				selectionRectangle.right = selectionRectangle.left;
				selectionRectangle.bottom = selectionRectangle.top;

				selectedXCorner = &selectionRectangle.right;
				selectedYCorner = &selectionRectangle.bottom;
				BOOL repaint = InvalidateRect(window, NULL, TRUE);
			}
			else if (cursorInSelection) {
				drag = TRUE;
			}
			return 0;
		}

		case WM_MOUSEMOVE: {
			POINT point = GetPoint(lParameter);
			if (wParameter == MK_LBUTTON && !drag) {
				if (selectedXCorner) *selectedXCorner = point.x;
				if (selectedYCorner) *selectedYCorner = point.y;
				BOOL repaint = InvalidateRect(window, NULL, TRUE);
			}
			else if (wParameter == MK_LBUTTON && drag) {
				POINT difference = GetDifference(point, previousPosition);
				selectionRectangle = TranslateRectangle(selectionRectangle, difference);
				BOOL repaint = InvalidateRect(window, NULL, TRUE);
			}
			previousPosition = point;
			return 0;
		}

		case WM_LBUTTONUP:
			// Normalize rectangle on release to ensure that the corner coordinates are consistent for subsequent rectangle transformations
			selectionRectangle = GetNormalizedRectangle(selectionRectangle);
			drag = FALSE;


		case WM_PAINT:
			PAINTSTRUCT paint;
			HDC client = BeginPaint(window, &paint);

			HDC scene = CreateCompatibleDC(client);
			HBITMAP sceneBitmap = CreateCompatibleBitmap(client, SCREEN_WIDTH, SCREEN_HEIGHT);
			HBITMAP previousSceneBitmap = SelectObject(scene, sceneBitmap);
			HBRUSH backgroundColor = CreateSolidBrush(RGB(0, 0, 0));

			assert(FillRect(scene, &screenRectangle, backgroundColor));
			BLENDFUNCTION blend = { 0 };
			blend.BlendOp = AC_SRC_OVER;
			blend.SourceConstantAlpha = 128;
			blend.AlphaFormat = AC_SRC_ALPHA;
			BOOL blended = GdiAlphaBlend(scene, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, memory, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, blend);

			if (HasArea(displayRectangle)) {
				BitBlt(scene, displayRectangle.left, displayRectangle.top, displayRectangle.right - displayRectangle.left, displayRectangle.bottom - displayRectangle.top,
					   memory, displayRectangle.left, displayRectangle.top, SRCCOPY);

				// Paint anchor boxes
				PaintAnchor(scene, anchors.topLeft);
				PaintAnchor(scene, anchors.topMid);
				PaintAnchor(scene, anchors.topRight);
				PaintAnchor(scene, anchors.midLeft);
				PaintAnchor(scene, anchors.midRight);
				PaintAnchor(scene, anchors.bottomLeft);
				PaintAnchor(scene, anchors.bottomMid);
				PaintAnchor(scene, anchors.bottomRight);
			}

			BitBlt(client, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, scene, 0, 0, SRCCOPY);

			// Clean up
			DeleteObject(backgroundColor);
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