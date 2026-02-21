#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <assert.h>
#include <windowsX.h>
#include <shlwapi.h>	// https://stackoverflow.com/a/49674208/32242805
#include <stdio.h>
#include <stdint.h>
#define STBIW_WINDOWS_UTF8
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define VK_A 0x41
#define VK_S 0x53
#define VK_F 0x46
#define VK_W 0x57
#define VK_C 0x43

#define VK_Z 0x5A
#define VK_Y 0x59

#define VK_V 0x56
#define VK_E 0x45
#define VK_H 0x48
#define VK_B 0x42

#define ARRAY_LENGTH(array) (sizeof(array) / sizeof(array[0]))

#define SWAP(TYPE, x, y) \
do {					 \
	TYPE temp;			 \
	temp = x;			 \
	x = y;				 \
	y = temp;			 \
} while (0)

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

#define MIDPOINT(x, y) (((x) + (y)) / 2)

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#define SCREEN_HANDLE NULL
#define SCREEN_AREA (SCREEN_WIDTH * SCREEN_HEIGHT)
#define BOX_SIZE 6 * 4

static int SCREEN_WIDTH = 0;
static int SCREEN_HEIGHT = 0;
static RECT screenRectangle = { 0 };
static RECT selectionRectangle = { 0 };
static HDC screen;
static HDC memory;
static HBITMAP memoryBitmap;
static HBITMAP previousMemoryBitmap;
static wchar_t *fileDirectory = NULL;
static CRITICAL_SECTION criticalSection;
static BOOL outlineSelection = FALSE;

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

RECT NormalizeAndTruncate(RECT r) {
	return GetTruncatedRectangle(GetNormalizedRectangle(r));
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
BOOL FileExists(LPCWSTR path) {
	DWORD attributes = GetFileAttributes(path);
	return attributes != INVALID_FILE_ATTRIBUTES;
}

// https://stackoverflow.com/a/6218445/32242805
BOOL DirectoryExists(LPCWSTR path) {
	DWORD attributes = GetFileAttributes(path);
	return (attributes != INVALID_FILE_ATTRIBUTES) && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

typedef struct {
	int selectionArea;
	RECT selectionRectangle;
	uint32_t *screenPixels;
	int screenWidth;
	wchar_t *fileDirectory;
	int selectionWidth;
	int selectionHeight;
} SaveScreenshotParameter;

DWORD WINAPI SaveScreenshot(LPVOID parameter) {
	SaveScreenshotParameter args = *((SaveScreenshotParameter *) parameter);

	uint32_t* selectionPixels = malloc(sizeof(uint32_t) * args.selectionArea);

	int i = 0;
	for (int y = args.selectionRectangle.top; y < args.selectionRectangle.bottom; y++) {
		for (int x = args.selectionRectangle.left; x < args.selectionRectangle.right; x++) {
			selectionPixels[i] = BGRAtoRGBA(args.screenPixels[(y * args.screenWidth) + x]);
			i += 1;
		}
	}

	EnterCriticalSection(&criticalSection);

	wchar_t filePath[MAX_PATH + 1 + 1] = L"";
	wchar_t fileName[MAX_PATH + 1 + 1] = L"";
	int n = 1;
	do {
		swprintf(fileName, MAX_PATH + 1 + 1, L"Screenshot_%d.png", n++);

		PathCombine(filePath, args.fileDirectory, fileName);
		if (wcslen(filePath) > MAX_PATH) {
			// TODO: Double check
			free(selectionPixels);
			free(args.screenPixels);
			free(args.fileDirectory);
			free(parameter);

			return 1;
		}
	} while (FileExists(filePath));

	char outputLocation[MAX_PATH + 1] = "";
	stbiw_convert_wchar_to_utf8(outputLocation, MAX_PATH + 1, filePath);
	int imageWritten = stbi_write_png(outputLocation, args.selectionWidth, args.selectionHeight,
		4, selectionPixels, args.selectionWidth * sizeof(uint32_t));

	// TODO: Double check
	free(selectionPixels);
	free(args.screenPixels);
	free(args.fileDirectory);
	free(parameter);

	LeaveCriticalSection(&criticalSection);

	return 0;
}

BOOL CopyToClipboard(HWND window, char *data, int size, UINT format) {
	// https://stackoverflow.com/a/72282181/32242805
	HGLOBAL allocatedMemoryObject = GlobalAlloc(GMEM_MOVEABLE, size);
	if (!allocatedMemoryObject)
		return FALSE;

	if (!OpenClipboard(window)) {
		GlobalFree(allocatedMemoryObject);
		return FALSE;
	}

	EmptyClipboard();
	char *allocatedMemory = GlobalLock(allocatedMemoryObject);
	if (allocatedMemory)
		memcpy(allocatedMemory, data, size);

	GlobalUnlock(allocatedMemoryObject);
	SetClipboardData(format, allocatedMemoryObject);
	CloseClipboard();
	return TRUE;
}

LRESULT CopySelectionToClipboard(HWND window) {
	selectionRectangle = NormalizeAndTruncate(selectionRectangle);
	const int SELECTION_WIDTH = GetWidth(selectionRectangle);
	const int SELECTION_HEIGHT = GetHeight(selectionRectangle);
	const int SELECTION_AREA = SELECTION_WIDTH * SELECTION_HEIGHT;

	if (!SELECTION_AREA) return 1;

	ShowWindow(window, SW_HIDE);

	BITMAPINFOHEADER header = { 0 };
	header.biSize = sizeof(header);
	header.biWidth = SELECTION_WIDTH;
	header.biHeight = SELECTION_HEIGHT;
	header.biPlanes = 1;
	header.biBitCount = 32;
	header.biCompression = BI_RGB;
	BITMAPINFO info = { 0 };
	info.bmiHeader = header;

	HDC copy = CreateCompatibleDC(memory);
	HBITMAP copyBitmap = CreateCompatibleBitmap(memory, SELECTION_WIDTH, SELECTION_HEIGHT);
	HBITMAP previousCopyBitmap = SelectObject(copy, copyBitmap);

	BitBlt(copy, 0, 0, SELECTION_WIDTH, SELECTION_HEIGHT,
		memory, selectionRectangle.left, selectionRectangle.top, SRCCOPY);

	int headerAndPixelsSize = sizeof(header) + (sizeof(uint32_t) * SELECTION_AREA);
	char *headerAndPixels = malloc(headerAndPixelsSize);
	BITMAPINFOHEADER *headerPart = (BITMAPINFOHEADER *) headerAndPixels;
	uint32_t *selectionPixels = (uint32_t *) (headerAndPixels + sizeof(header));

	*headerPart = header;
	int scanLinesCopied = GetDIBits(copy, copyBitmap, 0, SELECTION_HEIGHT, selectionPixels, &info, DIB_RGB_COLORS);

	CopyToClipboard(window, headerAndPixels, headerAndPixelsSize, CF_DIB);

	// Clean up
	free(headerAndPixels);
	assert(SelectObject(copy, previousCopyBitmap) == copyBitmap);
	DeleteDC(copy);
	DeleteObject(copyBitmap);

	return 0;
}

INPUT KeyInput(WORD virtualKeyCode, BOOL keyUp) {
	INPUT input = { 0 };
	input.type = INPUT_KEYBOARD;
	input.ki.wVk = virtualKeyCode;
	if (keyUp) input.ki.dwFlags = KEYEVENTF_KEYUP;
	return input;
}

RECT RectangleToSquare(RECT a) {
	LONG length = MAX(GetWidth(a), GetHeight(a));
	return (RECT) {
		.left = a.left,
		.top = a.top,
		.right = a.left + length,
		.bottom = a.top + length
	};
}

typedef struct Selections {
	RECT data;
	struct Selections *prev;
	struct Selections *next;
} Selections;

void SelectionsFree(Selections *xs) {
	if (xs) {
		Selections *next = xs->next;
		free(xs);
		SelectionsFree(next);
	}
}

Selections *SelectionsAdd(Selections *prev, RECT data) {
	Selections *selection = malloc(sizeof(Selections));
	selection->data = data;
	selection->prev = prev;
	selection->next = NULL;

	if (prev) {
		SelectionsFree(prev->next);
		prev->next = selection;
	}

	return selection;
}

Selections *SelectionsCreate(RECT data) {
	return SelectionsAdd(NULL, data);
}

int HandleKeyDown(HWND window, UINT message, WPARAM wParameter, LPARAM lParameter, Selections **currentSelection) {
	switch (wParameter) {
		case VK_ESCAPE:
			ShowWindow(window, SW_HIDE);
			return 0;

		case VK_E:
			if (!(GetAsyncKeyState(VK_CONTROL) & 0x8000) || CopySelectionToClipboard(window) != 0) return 0;

			SHELLEXECUTEINFOW execInfo = { 0 };
			execInfo.cbSize = sizeof(execInfo);
			execInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
			execInfo.lpVerb = L"open";
			execInfo.lpFile = L"mspaint";
			execInfo.nShow = SW_MAXIMIZE;
			ShellExecuteExW(&execInfo);
			Sleep(250);
			CloseHandle(execInfo.hProcess);

			// https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput
			INPUT inputs[100] = { 0 };
			ZeroMemory(inputs, sizeof(inputs));

			int i = 0;
			inputs[i++] = KeyInput(VK_CONTROL, FALSE);
			inputs[i++] = KeyInput(VK_V, FALSE);
			inputs[i++] = KeyInput(VK_V, TRUE);
			inputs[i++] = KeyInput(VK_CONTROL, TRUE);

			inputs[i++] = KeyInput(VK_CONTROL, FALSE);
			inputs[i++] = KeyInput(VK_E, FALSE);
			inputs[i++] = KeyInput(VK_E, TRUE);
			inputs[i++] = KeyInput(VK_CONTROL, TRUE);

			wchar_t selectionWidth[5 + 1 + 1] = { 0 };
			wchar_t selectionHeight[5 + 1 + 1] = { 0 };
			swprintf(selectionWidth, sizeof(selectionWidth), L"%d", GetWidth(selectionRectangle));
			swprintf(selectionHeight, sizeof(selectionHeight), L"%d", GetHeight(selectionRectangle));

			int j = 0;
			while (selectionWidth[j++]) {
				WORD numberKeyCode = selectionWidth[j - 1];
				inputs[i++] = KeyInput(numberKeyCode, FALSE);
				inputs[i++] = KeyInput(numberKeyCode, TRUE);
			}

			inputs[i++] = KeyInput(VK_TAB, FALSE);
			inputs[i++] = KeyInput(VK_TAB, TRUE);

			j = 0;
			while (selectionHeight[j++]) {
				WORD numberKeyCode = selectionHeight[j - 1];
				inputs[i++] = KeyInput(numberKeyCode, FALSE);
				inputs[i++] = KeyInput(numberKeyCode, TRUE);
			}

			inputs[i++] = KeyInput(VK_RETURN, FALSE);
			inputs[i++] = KeyInput(VK_RETURN, TRUE);

			inputs[i++] = KeyInput(VK_MENU, FALSE);
			inputs[i++] = KeyInput(VK_H, FALSE);
			inputs[i++] = KeyInput(VK_H, TRUE);
			inputs[i++] = KeyInput(VK_MENU, TRUE);

			inputs[i++] = KeyInput(VK_B, FALSE);
			inputs[i++] = KeyInput(VK_B, TRUE);

			inputs[i++] = KeyInput(VK_RETURN, FALSE);
			inputs[i++] = KeyInput(VK_RETURN, TRUE);

			assert(i < ARRAY_LENGTH(inputs));
			UINT sent = SendInput(ARRAYSIZE(inputs), inputs, sizeof(INPUT));

			return 0;

		case VK_C:
			if (!(GetAsyncKeyState(VK_CONTROL) & 0x8000)) return 0;
			return CopySelectionToClipboard(window);

		case VK_W:
			if (!(GetAsyncKeyState(VK_CONTROL) & 0x8000)) return 0;

			if (HasArea(selectionRectangle)) {
				selectionRectangle = (RECT){ 0 };
				outlineSelection = FALSE;
			}
			else ShowWindow(window, SW_HIDE);

			return 0;

		case VK_A:
			if (!(GetAsyncKeyState(VK_CONTROL) & 0x8000)) return 0;
			selectionRectangle = screenRectangle;
			outlineSelection = TRUE;
			return 0;

		case VK_F:
			outlineSelection = !outlineSelection;
			return 0;

		case VK_Z:
			if (!(GetAsyncKeyState(VK_CONTROL) & 0x8000)) return 0;

			if (*currentSelection && (*currentSelection)->prev) {
				*currentSelection = (*currentSelection)->prev;
				selectionRectangle = (*currentSelection)->data;
			}

			return 0;

		case VK_Y:
			if (!(GetAsyncKeyState(VK_CONTROL) & 0x8000)) return 0;

			if (*currentSelection && (*currentSelection)->next) {
				*currentSelection = (*currentSelection)->next;
				selectionRectangle = (*currentSelection)->data;
			}

			return 0;

		case VK_S: {
			// If 's' is pressed while CTRL is not, do not save
			if (!(GetAsyncKeyState(VK_CONTROL) & 0x8000)) return 0;

			selectionRectangle = GetTruncatedRectangle(GetNormalizedRectangle(selectionRectangle));
			const int SELECTION_WIDTH = GetWidth(selectionRectangle);
			const int SELECTION_HEIGHT = GetHeight(selectionRectangle);
			const int SELECTION_AREA = SELECTION_WIDTH * SELECTION_HEIGHT;

			// Ensure empty screenshot can't be saved
			if (!SELECTION_AREA) return 0;

			ShowWindow(window, SW_HIDE);

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

			SaveScreenshotParameter *parameter = malloc(sizeof(SaveScreenshotParameter));
			parameter->selectionArea = SELECTION_AREA;
			parameter->selectionRectangle = selectionRectangle;
			parameter->screenPixels = screenPixels;
			parameter->screenWidth = SCREEN_WIDTH;
			wchar_t *fileDirectoryCopy = malloc(sizeof(wchar_t) * (wcslen(fileDirectory) + 1));
			wcscpy(fileDirectoryCopy, fileDirectory);
			parameter->fileDirectory = fileDirectoryCopy;
			parameter->selectionWidth = SELECTION_WIDTH;
			parameter->selectionHeight = SELECTION_HEIGHT;

			HANDLE thread = CreateThread(NULL, 0, SaveScreenshot, parameter, 0, NULL);
			CloseHandle(thread);
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

AnchorBoxes FitBoxes(AnchorBoxes boxes, RECT fit) {
	boxes.topMid.left = fit.left;
	boxes.topMid.right = fit.right;

	boxes.bottomMid.left = fit.left;
	boxes.bottomMid.right = fit.right;

	boxes.midLeft.top = fit.top;
	boxes.midLeft.bottom = fit.bottom;

	boxes.midRight.top = fit.top;
	boxes.midRight.bottom = fit.bottom;
	return boxes;
}

HCURSOR GetCursor(POINT point, RECT displayRectangle, AnchorBoxes boxes) {
	// If selection is not visible, show default cursor
	if (!HasArea(displayRectangle))													  return LoadCursor(NULL, IDC_ARROW);
	else if (PtInRect(&boxes.topLeft, point) || PtInRect(&boxes.bottomRight, point))  return LoadCursor(NULL, IDC_SIZENWSE);
	else if (PtInRect(&boxes.bottomLeft, point) || PtInRect(&boxes.topRight, point))  return LoadCursor(NULL, IDC_SIZENESW);
	else if (PtInRect(&boxes.midLeft, point) || PtInRect(&boxes.midRight, point))	  return LoadCursor(NULL, IDC_SIZEWE);
	else if (PtInRect(&boxes.topMid, point) || PtInRect(&boxes.bottomMid, point))	  return LoadCursor(NULL, IDC_SIZENS);
	else if (PtInRect(&displayRectangle, point))									  return LoadCursor(NULL, IDC_SIZEALL);
	return LoadCursor(NULL, IDC_ARROW);
}

RECT GetUpdateRectangle(RECT before, RECT after, int padding) {
	before = NormalizeAndTruncate(before);
	after = NormalizeAndTruncate(after);
	return (RECT) {
		.left = MIN(before.left, after.left) - padding,
		.top = MIN(before.top, after.top) - padding,
		.right = MAX(before.right, after.right) + padding,
		.bottom = MAX(before.bottom, after.bottom) + padding
	};
}

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParameter, LPARAM lParameter) {
	static BOOL drag = FALSE;
	static POINT previousPosition;
	static LONG *selectedXCorner = NULL;
	static LONG *selectedYCorner = NULL;

	static Selections *selections = NULL;
	static Selections *currentSelection = NULL;

	RECT displayRectangle = GetTruncatedRectangle(GetNormalizedRectangle(selectionRectangle));
	Anchors anchors = GetAnchors(displayRectangle);
	AnchorBoxes boxes = GetAnchorBoxes(anchors);
	boxes = FitBoxes(boxes, displayRectangle);
	static POINT point;
	GetCursorPos(&point);

	switch (message) {
		case WM_SHOWWINDOW:
			BOOL windowShown = wParameter;
			if (!windowShown) outlineSelection = FALSE;
			return DefWindowProc(window, message, wParameter, lParameter);


		case WM_ACTIVATE:
			BOOL activationStatus = HIWORD(wParameter);
			if (activationStatus == WA_INACTIVE) ShowWindow(window, SW_HIDE);
			return DefWindowProc(window, message, wParameter, lParameter);

		case WM_DISPLAYCHANGE:
			if (IsWindowVisible(window)) ShowWindow(window, SW_HIDE);

			assert(SelectObject(memory, previousMemoryBitmap) == memoryBitmap);
			DeleteDC(memory);
			DeleteObject(memoryBitmap);
			memory = CreateCompatibleDC(screen);

			SCREEN_WIDTH = GetSystemMetrics(SM_CXSCREEN);
			SCREEN_HEIGHT = GetSystemMetrics(SM_CYSCREEN);

			// Create screen compatible bitmap and associate it with the memory device context
			memoryBitmap = CreateCompatibleBitmap(screen, SCREEN_WIDTH, SCREEN_HEIGHT);
			HBITMAP previousMemoryBitmap = SelectObject(memory, memoryBitmap);

			return 0;


		case WM_HOTKEY:
			if (!IsWindowVisible(window)) {
				selectionRectangle = (RECT){ 0 };
				// Transfer color data from screen to memory
				BOOL transferred = BitBlt(memory, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, screen, 0, 0, SRCCOPY);
				if (GetForegroundWindow() != window) SetForegroundWindow(window);
				SetWindowPos(window, HWND_TOP, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SWP_SHOWWINDOW);
				// BringWindowToTop(window);
			}
			return 0;

		case WM_KEYDOWN:
			if (HandleKeyDown(window, message, wParameter, lParameter, &currentSelection) == 0) {
				RECT update = GetUpdateRectangle(displayRectangle, selectionRectangle, BOX_SIZE / 2);
				BOOL repaint = InvalidateRect(window, &update, TRUE);
				return 0;
			}
			return 1;

		case WM_SETCURSOR:
			// Update cursor based on position while left click is not held
			SHORT leftClick = GetAsyncKeyState(VK_LBUTTON) & 0x8000;
			if (!leftClick) SetCursor(GetCursor(point, displayRectangle, boxes));
			// If left click is held and selection is not visible, show a default resize icon
			else if (leftClick && !HasArea(selectionRectangle)) SetCursor(LoadCursor(NULL, IDC_SIZENWSE));
			return TRUE;

		case WM_LBUTTONDOWN: {
			BOOL cursorInSelection = PtInRect(&displayRectangle, point);

			if (PtInRect(&boxes.topLeft, point)) {
				selectedXCorner = &selectionRectangle.left;
				selectedYCorner = &selectionRectangle.top;
			}
			else if (PtInRect(&boxes.bottomRight, point)) {
				selectedXCorner = &selectionRectangle.right;
				selectedYCorner = &selectionRectangle.bottom;
			}
			else if (PtInRect(&boxes.bottomLeft, point)) {
				selectedXCorner = &selectionRectangle.left;
				selectedYCorner = &selectionRectangle.bottom;
			}
			else if (PtInRect(&boxes.topRight, point)) {
				selectedXCorner = &selectionRectangle.right;
				selectedYCorner = &selectionRectangle.top;
			}
			else if (PtInRect(&boxes.midLeft, point)) {
				selectedXCorner = &selectionRectangle.left;
				selectedYCorner = NULL;

				if (GetAsyncKeyState(VK_LSHIFT) & 0x8000) selectionRectangle.left = selectionRectangle.right - GetHeight(selectionRectangle);
			}
			else if (PtInRect(&boxes.midRight, point)) {
				selectedXCorner = &selectionRectangle.right;
				selectedYCorner = NULL;

				if (GetAsyncKeyState(VK_LSHIFT) & 0x8000) selectionRectangle.right = selectionRectangle.left + GetHeight(selectionRectangle);
			}
			else if (PtInRect(&boxes.topMid, point)) {
				selectedXCorner = NULL;
				selectedYCorner = &selectionRectangle.top;

				if (GetAsyncKeyState(VK_LSHIFT) & 0x8000) selectionRectangle.top = selectionRectangle.bottom - GetWidth(selectionRectangle);
			}
			else if (PtInRect(&boxes.bottomMid, point)) {
				selectedXCorner = NULL;
				selectedYCorner = &selectionRectangle.bottom;

				if (GetAsyncKeyState(VK_LSHIFT) & 0x8000) selectionRectangle.bottom = selectionRectangle.top + GetWidth(selectionRectangle);
			}

			// Reset selection
			else if (!cursorInSelection) {
				selectionRectangle.left = point.x;
				selectionRectangle.top = point.y;
				selectionRectangle.right = selectionRectangle.left;
				selectionRectangle.bottom = selectionRectangle.top;

				selectedXCorner = &selectionRectangle.right;
				selectedYCorner = &selectionRectangle.bottom;

				// Ensure cursor is set on a new selection
				SetCursor(LoadCursor(NULL, IDC_SIZENWSE));
			}
			else if (cursorInSelection) {
				drag = TRUE;

				if (GetAsyncKeyState(VK_LSHIFT) & 0x8000) selectionRectangle = RectangleToSquare(selectionRectangle);
			}

			// Track selection history in list
			if (!selections) {
				selections = SelectionsCreate(selectionRectangle);
				currentSelection = selections;
			}
			else
				currentSelection = SelectionsAdd(currentSelection, selectionRectangle);

			RECT update = GetUpdateRectangle(displayRectangle, selectionRectangle, BOX_SIZE / 2);
			BOOL repaint = InvalidateRect(window, &update, TRUE);
			return 0;
		}

		case WM_MOUSEMOVE: {
			// https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-mousemove
			if ((wParameter & MK_LBUTTON) && !drag) {
				if (selectedXCorner) *selectedXCorner = point.x;
				if (selectedYCorner) *selectedYCorner = point.y;
				RECT update = GetUpdateRectangle(displayRectangle, selectionRectangle, BOX_SIZE / 2);
				BOOL repaint = InvalidateRect(window, &update, TRUE);
			}
			else if ((wParameter & MK_LBUTTON) && drag) {
				POINT difference = GetDifference(point, previousPosition);
				selectionRectangle = TranslateRectangle(selectionRectangle, difference);
				RECT update = GetUpdateRectangle(displayRectangle, selectionRectangle, BOX_SIZE / 2);
				BOOL repaint = InvalidateRect(window, &update, TRUE);
			}
			previousPosition = point;
			return 0;
		}

		case WM_LBUTTONUP: {
			// Normalize rectangle on release to ensure that the corner coordinates are consistent for subsequent rectangle transformations
			selectionRectangle = GetTruncatedRectangle(GetNormalizedRectangle(selectionRectangle));

			currentSelection->data = selectionRectangle;

			// If selection is not visible when left click is released, show default cursor
			if (!HasArea(selectionRectangle)) SetCursor(LoadCursor(NULL, IDC_ARROW));
			drag = FALSE;

			RECT update = GetUpdateRectangle(displayRectangle, selectionRectangle, BOX_SIZE / 2);
			BOOL repaint = InvalidateRect(window, &update, TRUE);
		}


		case WM_PAINT:
			PAINTSTRUCT paint;
			HDC client = BeginPaint(window, &paint);

			RECT update = paint.rcPaint;
			RECT sceneCoords = { .left = 0, .top = 0, .right = GetWidth(update), .bottom = GetHeight(update) };

			HDC scene = CreateCompatibleDC(client);
			HBITMAP sceneBitmap = CreateCompatibleBitmap(client, GetWidth(update), GetHeight(update));
			HBITMAP previousSceneBitmap = SelectObject(scene, sceneBitmap);
			HBRUSH backgroundColor = CreateSolidBrush(RGB(0, 0, 0));

			assert(FillRect(scene, &sceneCoords, backgroundColor));
			BLENDFUNCTION blend = { 0 };
			blend.BlendOp = AC_SRC_OVER;
			blend.SourceConstantAlpha = 128;
			blend.AlphaFormat = AC_SRC_ALPHA;
			BOOL blended = GdiAlphaBlend(scene, 0, 0, GetWidth(update), GetHeight(update),
										 memory, update.left, update.top, GetWidth(update), GetHeight(update), blend);

			if (HasArea(displayRectangle)) {
				// Display rectangle must be relative to the update region, not the client
				BitBlt(scene, (displayRectangle.left - update.left), (displayRectangle.top - update.top), displayRectangle.right - displayRectangle.left, displayRectangle.bottom - displayRectangle.top,
					   memory, displayRectangle.left, displayRectangle.top, SRCCOPY);

				if (outlineSelection) {
					COLORREF black = RGB(0, 0, 0);
					HPEN pen = CreatePen(PS_DOT, 1, black);
					SelectObject(scene, pen);

					MoveToEx(scene, displayRectangle.left - update.left, displayRectangle.top - update.top, NULL);
					LineTo(scene, displayRectangle.right - update.left, displayRectangle.top - update.top);
					LineTo(scene, displayRectangle.right - update.left, displayRectangle.bottom - update.top);
					LineTo(scene, displayRectangle.left - update.left, displayRectangle.bottom - update.top);
					LineTo(scene, displayRectangle.left - update.left, displayRectangle.top - update.top);

					DeleteObject(pen);
				}
			}

			BitBlt(client, update.left, update.top, GetWidth(update), GetHeight(update), scene, 0, 0, SRCCOPY);

			// Clean up
			DeleteObject(backgroundColor);
			assert(SelectObject(scene, previousSceneBitmap) == sceneBitmap);
			DeleteDC(scene);
			DeleteObject(sceneBitmap);
			EndPaint(window, &paint);
			return 0;

		case WM_DESTROY:
			// Free selection history
			SelectionsFree(selections);
			PostQuitMessage(0);
			return 0;

		default:
			return DefWindowProc(window, message, wParameter, lParameter);
	}
}

int WINAPI wWinMain(HINSTANCE appInstance, HINSTANCE previousInstance, PWSTR commandLine, int visibility) {
	// https://stackoverflow.com/a/33531179/32242805
	// https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createmutexw
	HANDLE singleInstanceMutex = CreateMutex(NULL, TRUE, L"Single Instance Mutex for Screenshot Application");
	if (GetLastError()) return 1;

	if (wcslen(commandLine)) {
		fileDirectory = commandLine;
		assert(DirectoryExists(fileDirectory));
	}

	SCREEN_WIDTH = GetSystemMetrics(SM_CXSCREEN);
	SCREEN_HEIGHT = GetSystemMetrics(SM_CYSCREEN);

	screenRectangle.left = 0;
	screenRectangle.top = 0;
	// "By convention, the right and bottom edges of the rectangle are normally considered exclusive."
	screenRectangle.right = SCREEN_WIDTH;
	screenRectangle.bottom = SCREEN_HEIGHT;

	screen = GetDC(SCREEN_HANDLE);
	memory = CreateCompatibleDC(screen);

	// Create screen compatible bitmap and associate it with the memory device context
	memoryBitmap = CreateCompatibleBitmap(screen, SCREEN_WIDTH, SCREEN_HEIGHT);
	previousMemoryBitmap = SelectObject(memory, memoryBitmap);

	WNDCLASS windowClass = { 0 };
	windowClass.hInstance = appInstance;
	windowClass.lpszClassName = L"Window Class";
	windowClass.lpfnWndProc = WindowProcedure;
	windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
	RegisterClass(&windowClass);

	HWND window = CreateWindow(windowClass.lpszClassName, L"", WS_POPUP,
				 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT,
				 NULL, NULL, appInstance, NULL);

	RegisterHotKey(window, 0, NULL, VK_SNAPSHOT);
	ShowWindow(window, SW_HIDE);

	InitializeCriticalSection(&criticalSection);

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
	ReleaseDC(SCREEN_HANDLE, screen);
	assert(SelectObject(memory, previousMemoryBitmap) == memoryBitmap);
	DeleteDC(memory);
	DeleteObject(memoryBitmap);

	DeleteCriticalSection(&criticalSection);
	ReleaseMutex(singleInstanceMutex);
	CloseHandle(singleInstanceMutex);

	return 0;
}