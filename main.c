#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <assert.h>
#include <windowsX.h>
#include <stdio.h>
#include <stdint.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#define VK_S 0x53

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
static HBITMAP memoryBitmap;

RECT GetNormalizedRectangle(RECT rectangle) {
	if (rectangle.right - rectangle.left < 0) SWAP(LONG, rectangle.right, rectangle.left);
	if (rectangle.bottom - rectangle.top < 0) SWAP(LONG, rectangle.bottom, rectangle.top);
	return rectangle;
}

RECT GetTruncatedRectangle(RECT r) {
	if (r.left < 0) r.left = 0;
	if (r.top < 0) r.top = 0;
	if (r.right > SCREEN_WIDTH) r.right = SCREEN_WIDTH;
	if (r.bottom > SCREEN_HEIGHT) r.bottom = SCREEN_HEIGHT;
	return r;
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

LONG GetWidth(RECT r) {
	return r.right - r.left;
}

LONG GetHeight(RECT r) {
	return r.bottom - r.top;
}

#pragma pack(push, 1)
typedef struct {
	uint8_t blue;
	uint8_t green;
	uint8_t red;
	uint8_t alpha;
} BGRA32;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
	uint8_t red;
	uint8_t green;
	uint8_t blue;
	uint8_t alpha;
} RGBA32;
#pragma pack(pop)

uint32_t BGRAtoRGBA(uint32_t value) {
	BGRA32 bgra = *((BGRA32 *) &value);
	RGBA32 rgba = { .red = bgra.red, .green = bgra.green, .blue = bgra.blue, .alpha =  bgra.alpha };
	uint32_t returnValue = *((uint32_t*) &rgba);
	return returnValue;
}

// https://stackoverflow.com/a/6218957
BOOL FileExists(LPCSTR path) {
	DWORD attributes = GetFileAttributesA(path);
	return attributes != INVALID_FILE_ATTRIBUTES;
}

int HandleKeyUp(HWND window, UINT message, WPARAM wParameter, LPARAM lParameter) {
	switch (wParameter) {
		case VK_ESCAPE:
			DestroyWindow(window);
			return 0;

		case VK_S: {
			DestroyWindow(window);

			selectionRectangle = GetTruncatedRectangle(GetNormalizedRectangle(selectionRectangle));

			// Specify format for the bitmap data to be returned
			BITMAPINFOHEADER header = { 0 };
			header.biSize = sizeof(header);
			header.biWidth = SCREEN_WIDTH;
			header.biHeight = -SCREEN_HEIGHT;
			header.biPlanes = 1;
			header.biBitCount = 32;
			header.biCompression = BI_RGB;
			BITMAPINFO info = { 0 };
			info.bmiHeader = header;

			// Capture instance of screen pixels
			uint32_t *screenPixels = malloc(sizeof(uint32_t) * SCREEN_AREA);
			int scanLinesCopied = GetDIBits(memory, memoryBitmap, 0, SCREEN_HEIGHT, screenPixels, &info, DIB_RGB_COLORS);

			const int SELECTION_WIDTH = GetWidth(selectionRectangle);
			const int SELECTION_HEIGHT = GetHeight(selectionRectangle);
			const int SELECTION_AREA = SELECTION_WIDTH * SELECTION_HEIGHT;
			uint32_t *selectionPixels = malloc(sizeof(uint32_t) * SELECTION_AREA);

			int i = 0;
			for (int y = selectionRectangle.top; y < selectionRectangle.bottom;  y++) {
				for (int x = selectionRectangle.left; x < selectionRectangle.right; x++) {
					selectionPixels[i] = BGRAtoRGBA(screenPixels[(y * SCREEN_WIDTH) + x]);
					i += 1;
				}
			}

			char fileName[MAX_PATH + 1 + 1] = "";
			int n = 1;
			do {
				sprintf(fileName, "Screenshot_%d.png", n++);
				if (strlen(fileName) > MAX_PATH) return 1;
			} while (FileExists(fileName));

			int imageWritten = stbi_write_png(fileName, SELECTION_WIDTH, SELECTION_HEIGHT,
											  4, selectionPixels, SELECTION_WIDTH * sizeof(uint32_t));
			free(selectionPixels);
			free(screenPixels);
			return 0;
		}

		default:
			return DefWindowProc(window, message, wParameter, lParameter);
	}
}

RECT GetBox(POINT p, const LONG size) {
	RECT box = { .left = p.x - size/2, .top = p.y - size/2, .right = p.x + size/2, .bottom = p.y + size/2};
	return box;
}

void PaintAnchor(HDC destination, POINT p, COLORREF color, LONG size) {
	HBRUSH boxColor = CreateSolidBrush(color);
	RECT box = GetBox(p, size);
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

typedef struct {
	RECT topLeft;
	RECT topMid;
	RECT topRight;

	RECT midLeft;
	RECT midRight;

	RECT bottomLeft;
	RECT bottomMid;
	RECT bottomRight;
} AnchorBoxes;

AnchorBoxes GetAnchorBoxes(Anchors anchors) {
	AnchorBoxes boxes;
	boxes.topLeft = GetBox(anchors.topLeft, BOX_SIZE);
	boxes.topMid = GetBox(anchors.topMid, BOX_SIZE);
	boxes.topRight = GetBox(anchors.topRight, BOX_SIZE);
	boxes.midLeft = GetBox(anchors.midLeft, BOX_SIZE);
	boxes.midRight = GetBox(anchors.midRight, BOX_SIZE);
	boxes.bottomLeft = GetBox(anchors.bottomLeft, BOX_SIZE);
	boxes.bottomMid = GetBox(anchors.bottomMid, BOX_SIZE);
	boxes.bottomRight = GetBox(anchors.bottomRight, BOX_SIZE);
	return boxes;
}

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParameter, LPARAM lParameter) {
	static BOOL drag = FALSE;
	static POINT previousPosition;
	static LONG *selectedXCorner = NULL;
	static LONG *selectedYCorner = NULL;
	RECT displayRectangle = GetTruncatedRectangle(GetNormalizedRectangle(selectionRectangle));
	Anchors anchors = GetAnchors(displayRectangle);

	switch (message) {
		case WM_KEYUP:
			return HandleKeyUp(window, message, wParameter, lParameter);

		case WM_LBUTTONDOWN: {
			POINT point = GetPoint(lParameter);
			BOOL cursorInSelection = PtInRect(&displayRectangle, point);

			AnchorBoxes boxes = GetAnchorBoxes(anchors);
			if (PtInRect(&boxes.topLeft, point)) {
				selectedXCorner = &selectionRectangle.left;
				selectedYCorner = &selectionRectangle.top;
			}
			else if (PtInRect(&boxes.topMid, point)) {
				selectedXCorner = NULL;
				selectedYCorner = &selectionRectangle.top;
			}
			else if (PtInRect(&boxes.topRight, point)) {
				selectedXCorner = &selectionRectangle.right;
				selectedYCorner = &selectionRectangle.top;
			}
			else if (PtInRect(&boxes.midLeft, point)) {
				selectedXCorner = &selectionRectangle.left;
				selectedYCorner = NULL;
			}
			else if (PtInRect(&boxes.midRight, point)) {
				selectedXCorner = &selectionRectangle.right;
				selectedYCorner = NULL;
			}
			else if (PtInRect(&boxes.bottomLeft, point)) {
				selectedXCorner = &selectionRectangle.left;
				selectedYCorner = &selectionRectangle.bottom;
			}
			else if (PtInRect(&boxes.bottomMid, point)) {
				selectedXCorner = NULL;
				selectedYCorner = &selectionRectangle.bottom;
			}
			else if (PtInRect(&boxes.bottomRight, point)) {
				selectedXCorner = &selectionRectangle.right;
				selectedYCorner = &selectionRectangle.bottom;
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
			selectionRectangle = GetTruncatedRectangle(GetNormalizedRectangle(selectionRectangle));
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
				COLORREF black = RGB(0, 0, 0);
				COLORREF white = RGB(255, 255, 255);
				PaintAnchor(scene, anchors.topLeft, white, BOX_SIZE);
				PaintAnchor(scene, anchors.topMid, white, BOX_SIZE);
				PaintAnchor(scene, anchors.topRight, white, BOX_SIZE);
				PaintAnchor(scene, anchors.midLeft, white, BOX_SIZE);
				PaintAnchor(scene, anchors.midRight, white, BOX_SIZE);
				PaintAnchor(scene, anchors.bottomLeft, white, BOX_SIZE);
				PaintAnchor(scene, anchors.bottomMid, white, BOX_SIZE);
				PaintAnchor(scene, anchors.bottomRight, white, BOX_SIZE);

				PaintAnchor(scene, anchors.topLeft, black, BOX_SIZE - 1);
				PaintAnchor(scene, anchors.topMid, black, BOX_SIZE - 1);
				PaintAnchor(scene, anchors.topRight, black, BOX_SIZE - 1);
				PaintAnchor(scene, anchors.midLeft, black, BOX_SIZE - 1);
				PaintAnchor(scene, anchors.midRight, black, BOX_SIZE - 1);
				PaintAnchor(scene, anchors.bottomLeft, black, BOX_SIZE - 1);
				PaintAnchor(scene, anchors.bottomMid, black, BOX_SIZE - 1);
				PaintAnchor(scene, anchors.bottomRight, black, BOX_SIZE - 1);
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
	memoryBitmap = CreateCompatibleBitmap(screen, SCREEN_WIDTH, SCREEN_HEIGHT);
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