// TODO: refactor, research dpi scaling, handle when selection history is out of memory, customizable selection key, ensure consistent resizing logic when rectangle is not normalized, gifs
#define _CRT_SECURE_NO_WARNINGS
#define STBIW_WINDOWS_UTF8
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include <windows.h>
#include <assert.h>
#include <windowsX.h>
#include <shlwapi.h>	// https://stackoverflow.com/a/49674208/32242805
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "rectangle.h"
#include "rectangle_list.h"
#include "file_info.h"
#include "aspect_ratio.h"
#include "rgba32.h"
#include "position.h"

#define VK_V 0x56
#define VK_E 0x45
#define VK_H 0x48
#define VK_B 0x42
#define VK_GRAVE VK_OEM_3

#define ARRAY_LEN(array) (sizeof(array) / sizeof(array[0]))

#define MIDPOINT(x, y) (((x) + (y)) / 2)

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#define SCREEN_HANDLE NULL
#define SCREEN_AREA (SCREEN_WIDTH * SCREEN_HEIGHT)
#define SELECTION_HITBOX_SIZE (6 * 4)
// https://superuser.com/a/1915000
#define MAX_SCREEN_WIDTH_LEN 6
#define MAX_SCREEN_HEIGHT_LEN 6

static int SCREEN_WIDTH = 0;
static int SCREEN_HEIGHT = 0;
static RECT screenRectangle = { 0 };
static RECT selectionRectangle = { 0 };
static HDC screenDeviceContext;
static HDC memoryDeviceContext;
static HBITMAP memoryBitmap;
static HBITMAP previousMemoryBitmap;
static wchar_t screenshotDirectory[MAX_PATH];
static wchar_t exeDirectory[MAX_PATH];
static wchar_t configPath[MAX_PATH];
static CRITICAL_SECTION criticalSection;
static BOOL showSelectionOutline = FALSE;
static wchar_t screenshotPrefix[MAX_PATH];
static RectangleNode *lastSavedSelection = NULL;

#define ID_HOTKEY_SCREEN_CAPTURE 0

HACCEL shortcutTable = NULL;
#define NUM_SHORTCUTS 13
#define ID_CLOSE 40002
#define ID_OUTLINE_SELECTION 40003
#define ID_RELOAD_CONFIG 40004
#define ID_OPEN_PAINT 40005
#define ID_COPY 40006
#define ID_DESELECT 40007
#define ID_SELECT_ALL 40008
#define ID_UNDO 40009
#define ID_REDO 40010
#define ID_UPSCALE 40011
#define ID_DOWNSCALE 40012
#define ID_SAVE 40013
#define ID_OPEN_CONFIG 40014

#define SHIFT_STRING L"SHIFT"
#define CTRL_STRING L"CTRL"
#define ALT_STRING L"ALT"
#define HEX_PREFIX L"0X"

typedef struct {
	int selectionArea;
	RECT selectionRectangle;
	uint32_t *screenPixels;
	int screenWidth;
	wchar_t *screenshotDirectory;
	int selectionWidth;
	int selectionHeight;
} SaveScreenshotParameter;

DWORD WINAPI SaveScreenshot(LPVOID parameter) {
	SaveScreenshotParameter args = *((SaveScreenshotParameter *) parameter);

	uint32_t* selectionPixels = malloc(sizeof(uint32_t) * args.selectionArea);
	if (!selectionPixels) return SaveScreenshotFree(selectionPixels, args.screenPixels, args.screenshotDirectory, parameter, TRUE);

	int i = 0;
	for (int y = args.selectionRectangle.top; y < args.selectionRectangle.bottom; y++) {
		for (int x = args.selectionRectangle.left; x < args.selectionRectangle.right; x++) {
			selectionPixels[i] = BGRA32toRGBA32(args.screenPixels[(y * args.screenWidth) + x]);
			i += 1;
		}
	}

	EnterCriticalSection(&criticalSection);

	wchar_t screenshotPath[MAX_PATH + 1 + 1] = L"";
	wchar_t screenshotName[MAX_PATH + 1 + 1] = L"";
	int n = 1;
	do {
		swprintf(screenshotName, MAX_PATH + 1 + 1, L"%s%d.png", screenshotPrefix, n++);

		PathCombine(screenshotPath, args.screenshotDirectory, screenshotName);
		if (wcslen(screenshotPath) > MAX_PATH) {
			// TODO: Double check
			free(selectionPixels);
			free(args.screenPixels);
			free(args.screenshotDirectory);
			free(parameter);

			return 1;
		}
	} while (FileOrDirectoryExists(screenshotPath));

	char convertedScreenshotPath[MAX_PATH + 1] = "";
	stbiw_convert_wchar_to_utf8(convertedScreenshotPath, MAX_PATH + 1, screenshotPath);
	int imageWritten = stbi_write_png(convertedScreenshotPath, args.selectionWidth, args.selectionHeight,
		4, selectionPixels, args.selectionWidth * sizeof(uint32_t));

	// TODO: Double check
	free(selectionPixels);
	free(args.screenPixels);
	free(args.screenshotDirectory);
	free(parameter);

	LeaveCriticalSection(&criticalSection);

	return 0;
}

BOOL CopyDataToClipboard(HWND window, char *data, int size, UINT format) {
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
	selectionRectangle = RectangleNormalizeTruncate(selectionRectangle, screenRectangle);
	const int SELECTION_WIDTH = RectangleWidth(selectionRectangle);
	const int SELECTION_HEIGHT = RectangleHeight(selectionRectangle);
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

	HDC copyDeviceContext = CreateCompatibleDC(memoryDeviceContext);
	HBITMAP copyBitmap = CreateCompatibleBitmap(memoryDeviceContext, SELECTION_WIDTH, SELECTION_HEIGHT);
	HBITMAP previousCopyBitmap = SelectObject(copyDeviceContext, copyBitmap);

	BitBlt(copyDeviceContext, 0, 0, SELECTION_WIDTH, SELECTION_HEIGHT,
		memoryDeviceContext, selectionRectangle.left, selectionRectangle.top, SRCCOPY);

	int headerAndPixelsSize = sizeof(header) + (sizeof(uint32_t) * SELECTION_AREA);
	char *headerAndPixels = malloc(headerAndPixelsSize);
	if (!headerAndPixels) {
		MessageBoxW(window, L"Could not copy selection to clipboard.", NULL, MB_OK | MB_ICONERROR);
	}
	else {
		BITMAPINFOHEADER *headerPart = (BITMAPINFOHEADER *)headerAndPixels;
		uint32_t *selectionPixels = (uint32_t *)(headerAndPixels + sizeof(header));

		*headerPart = header;
		int scanLinesCopied = GetDIBits(copyDeviceContext, copyBitmap, 0, SELECTION_HEIGHT, selectionPixels, &info, DIB_RGB_COLORS);

		CopyDataToClipboard(window, headerAndPixels, headerAndPixelsSize, CF_DIB);
	}

	// Clean up
	free(headerAndPixels);
	SelectObject(copyDeviceContext, previousCopyBitmap);
	DeleteDC(copyDeviceContext);
	DeleteObject(copyBitmap);

	return 0;
}

INPUT InputKeyMake(WORD virtualKeyCode, BOOL keyUp) {
	INPUT input = { 0 };
	input.type = INPUT_KEYBOARD;
	input.ki.wVk = virtualKeyCode;
	if (keyUp) input.ki.dwFlags = KEYEVENTF_KEYUP;
	return input;
}

int SaveScreenshotFree(uint32_t *selectionPixels, uint32_t *screenPixels, wchar_t *screenshotDirectory, SaveScreenshotParameter *parameter, BOOL error) {
	free(selectionPixels);
	free(screenPixels);
	free(screenshotDirectory);
	free(parameter);

	if (error) MessageBoxW(NULL, L"Screenshot could not be saved.", NULL, MB_OK | MB_ICONERROR);

	return 0;
}

int WindowOnShortcut(HWND window, UINT message, WPARAM wParameter, LPARAM lParameter) {
	static SIZE prevAspectRatio = { 0 };
	if (!AspectRatioIsPositive(prevAspectRatio)) {
		prevAspectRatio.cx = 1;
		prevAspectRatio.cy = 1;
	}

	SIZE aspectRatio = RectangleAspectRatio(selectionRectangle);

	LONG *selectionRight = RectangleRight(&selectionRectangle);
	LONG *selectionBottom = RectangleBottom(&selectionRectangle);

	WORD command = LOWORD(wParameter);
	switch (command) {
		case ID_CLOSE:
			ShowWindow(window, SW_HIDE);
			return 0;

		case ID_OUTLINE_SELECTION:
			showSelectionOutline = !showSelectionOutline;
			return 0;

		case ID_RELOAD_CONFIG:
			if (!ConfigLoad(window)) {
				DestroyWindow(window);
				return 1;
			}

			return 0;

		case ID_OPEN_PAINT:
			if (CopySelectionToClipboard(window) != 0) return 0;

			SHELLEXECUTEINFOW execInfo = { 0 };
			execInfo.cbSize = sizeof(execInfo);
			execInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
			execInfo.lpVerb = L"open";
			execInfo.lpFile = L"mspaint";
			execInfo.nShow = SW_MAXIMIZE;
			ShellExecuteExW(&execInfo);
			if (!execInfo.hProcess) {
				MessageBoxW(window, L"Paint could not be opened.", NULL, MB_OK | MB_ICONERROR);
				return 0;
			}

			Sleep(250);
			CloseHandle(execInfo.hProcess);

			// https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput
			INPUT inputs[100] = { 0 };
			ZeroMemory(inputs, sizeof(inputs));

			int i = 0;
			inputs[i++] = InputKeyMake(VK_CONTROL, FALSE);
			inputs[i++] = InputKeyMake(VK_V, FALSE);
			inputs[i++] = InputKeyMake(VK_V, TRUE);
			inputs[i++] = InputKeyMake(VK_CONTROL, TRUE);

			inputs[i++] = InputKeyMake(VK_CONTROL, FALSE);
			inputs[i++] = InputKeyMake(VK_E, FALSE);
			inputs[i++] = InputKeyMake(VK_E, TRUE);
			inputs[i++] = InputKeyMake(VK_CONTROL, TRUE);

			wchar_t selectionWidth[MAX_SCREEN_WIDTH_LEN] = { 0 };
			wchar_t selectionHeight[MAX_SCREEN_HEIGHT_LEN] = { 0 };
			swprintf(selectionWidth, ARRAY_LEN(selectionWidth), L"%d", RectangleWidth(selectionRectangle));
			swprintf(selectionHeight, ARRAY_LEN(selectionHeight), L"%d", RectangleHeight(selectionRectangle));

			int j = 0;
			while (selectionWidth[j++]) {
				WORD numberKeyCode = selectionWidth[j - 1];
				inputs[i++] = InputKeyMake(numberKeyCode, FALSE);
				inputs[i++] = InputKeyMake(numberKeyCode, TRUE);
			}

			inputs[i++] = InputKeyMake(VK_TAB, FALSE);
			inputs[i++] = InputKeyMake(VK_TAB, TRUE);

			j = 0;
			while (selectionHeight[j++]) {
				WORD numberKeyCode = selectionHeight[j - 1];
				inputs[i++] = InputKeyMake(numberKeyCode, FALSE);
				inputs[i++] = InputKeyMake(numberKeyCode, TRUE);
			}

			inputs[i++] = InputKeyMake(VK_RETURN, FALSE);
			inputs[i++] = InputKeyMake(VK_RETURN, TRUE);

			inputs[i++] = InputKeyMake(VK_MENU, FALSE);
			inputs[i++] = InputKeyMake(VK_H, FALSE);
			inputs[i++] = InputKeyMake(VK_H, TRUE);
			inputs[i++] = InputKeyMake(VK_MENU, TRUE);

			inputs[i++] = InputKeyMake(VK_B, FALSE);
			inputs[i++] = InputKeyMake(VK_B, TRUE);

			inputs[i++] = InputKeyMake(VK_RETURN, FALSE);
			inputs[i++] = InputKeyMake(VK_RETURN, TRUE);

			assert(i < ARRAY_LEN(inputs));
			UINT sent = SendInput(ARRAYSIZE(inputs), inputs, sizeof(INPUT));

			return 0;

		case ID_COPY:
			return CopySelectionToClipboard(window);

		case ID_DESELECT:
			if (RectangleHasArea(selectionRectangle)) {
				selectionRectangle = (RECT){ 0 };
				showSelectionOutline = FALSE;
			}
			else ShowWindow(window, SW_HIDE);

			return 0;

		case ID_SELECT_ALL:
			selectionRectangle = screenRectangle;
			showSelectionOutline = TRUE;
			return 0;

		case ID_UNDO:
			if (lastSavedSelection && lastSavedSelection->prev) {
				lastSavedSelection = RectangleListUndo(lastSavedSelection->prev);
				// Prevent current selection from being empty if possible when the first element is reached and empty
				if (!RectangleHasArea(lastSavedSelection->data)) lastSavedSelection = RectangleListRedo(lastSavedSelection->next);

				selectionRectangle = lastSavedSelection->data;
			}

			return 0;

		case ID_REDO:
			if (lastSavedSelection && lastSavedSelection->next) {
				lastSavedSelection = RectangleListRedo(lastSavedSelection->next);

				if (!RectangleHasArea(lastSavedSelection->data)) lastSavedSelection = RectangleListUndo(lastSavedSelection->prev);

				selectionRectangle = lastSavedSelection->data;
			}

			return 0;

		case ID_DOWNSCALE: {
			RECT selectionRectangleCopy = selectionRectangle;
			if (!AspectRatioEqual(aspectRatio, prevAspectRatio) && !AspectRatioIsPositive(aspectRatio)) {
				*selectionRight -= prevAspectRatio.cx;
				*selectionBottom -= prevAspectRatio.cy;
			}
			else {
				*selectionRight -= aspectRatio.cx;
				*selectionBottom -= aspectRatio.cy;
				prevAspectRatio = aspectRatio;
			}
			// Ensure this key can only decrease the size of the selection
			if (RectangleOutOfBounds(selectionRectangle, screenRectangle) || RectangleArea(selectionRectangle) > RectangleArea(selectionRectangleCopy) || !RectangleArea(selectionRectangle)) selectionRectangle = selectionRectangleCopy;

			return 0;
		}

		case ID_UPSCALE: {
			RECT selectionRectangleCopy = selectionRectangle;
			if (!AspectRatioEqual(aspectRatio, prevAspectRatio) && !AspectRatioIsPositive(aspectRatio)) {
				*selectionRight += prevAspectRatio.cx;
				*selectionBottom  += prevAspectRatio.cy;
			}
			else {
				*selectionRight += aspectRatio.cx;
				*selectionBottom += aspectRatio.cy;
				prevAspectRatio = aspectRatio;
			}
			if (RectangleOutOfBounds(selectionRectangle, screenRectangle) || !RectangleArea(selectionRectangle)) selectionRectangle = selectionRectangleCopy;

			return 0;
		}

		case ID_SAVE: {
			selectionRectangle = RectangleNormalizeTruncate(selectionRectangle, screenRectangle);
			const int SELECTION_WIDTH = RectangleWidth(selectionRectangle);
			const int SELECTION_HEIGHT = RectangleHeight(selectionRectangle);
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
			if (!screenPixels) return SaveScreenshotFree(NULL, screenPixels, NULL, NULL, TRUE);

			int scanLinesCopied = GetDIBits(memoryDeviceContext, memoryBitmap, 0, SCREEN_HEIGHT, screenPixels, &info, DIB_RGB_COLORS);

			SaveScreenshotParameter *parameter = malloc(sizeof(SaveScreenshotParameter));
			if (!parameter) return SaveScreenshotFree(NULL, screenPixels, NULL, parameter, TRUE);

			parameter->selectionArea = SELECTION_AREA;
			parameter->selectionRectangle = selectionRectangle;
			parameter->screenPixels = screenPixels;
			parameter->screenWidth = SCREEN_WIDTH;
			wchar_t *screenshotDirectoryCopy = malloc(sizeof(wchar_t) * (wcslen(screenshotDirectory) + 1));
			if (!screenshotDirectoryCopy) return SaveScreenshotFree(NULL, screenPixels, screenshotDirectoryCopy, parameter, TRUE);

			wcscpy(screenshotDirectoryCopy, screenshotDirectory);
			parameter->screenshotDirectory = screenshotDirectoryCopy;
			parameter->selectionWidth = SELECTION_WIDTH;
			parameter->selectionHeight = SELECTION_HEIGHT;

			HANDLE thread = CreateThread(NULL, 0, SaveScreenshot, parameter, 0, NULL);
			// Clean up
			if (!thread) return SaveScreenshotFree(NULL, screenPixels, screenshotDirectoryCopy, parameter, TRUE);
			CloseHandle(thread);
			return 0;
		}

		case ID_OPEN_CONFIG:
			ShowWindow(window, SW_HIDE);

			HANDLE config = CreateFileW(configPath, GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
			if (config == INVALID_HANDLE_VALUE) {
				return MessageBoxW(window, L"Config file could not be opened.", NULL, MB_OK | MB_ICONERROR);
			}
			CloseHandle(config);

			ShellExecuteW(window, L"open", CONFIG_FILE, NULL, exeDirectory, SW_SHOW);
			return 0;

		default:
			return DefWindowProc(window, message, wParameter, lParameter);
	}
}

RECT SelectionHitboxMake(POINT p, const LONG size) {
	RECT box = { .left = p.x - size/2, .top = p.y - size/2, .right = p.x + size/2, .bottom = p.y + size/2};
	return box;
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
} SelectionPoints;

SelectionPoints SelectionPointsMake(RECT r) {
	SelectionPoints anchors;

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
} SelectionHitboxes;

SelectionHitboxes SelectionHitboxesMake(SelectionPoints anchors) {
	SelectionHitboxes boxes;
	boxes.topLeft = SelectionHitboxMake(anchors.topLeft, SELECTION_HITBOX_SIZE);
	boxes.topMid = SelectionHitboxMake(anchors.topMid, SELECTION_HITBOX_SIZE);
	boxes.topRight = SelectionHitboxMake(anchors.topRight, SELECTION_HITBOX_SIZE);
	boxes.midLeft = SelectionHitboxMake(anchors.midLeft, SELECTION_HITBOX_SIZE);
	boxes.midRight = SelectionHitboxMake(anchors.midRight, SELECTION_HITBOX_SIZE);
	boxes.bottomLeft = SelectionHitboxMake(anchors.bottomLeft, SELECTION_HITBOX_SIZE);
	boxes.bottomMid = SelectionHitboxMake(anchors.bottomMid, SELECTION_HITBOX_SIZE);
	boxes.bottomRight = SelectionHitboxMake(anchors.bottomRight, SELECTION_HITBOX_SIZE);
	return boxes;
}

SelectionHitboxes SelectionHitboxesExtend(SelectionHitboxes boxes, RECT fit) {
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

HCURSOR WindowGetCursor(POINT point, RECT displayRectangle, SelectionHitboxes boxes) {
	// If selection is not visible, show default cursor
	if (!RectangleHasArea(displayRectangle))													  return LoadCursor(NULL, IDC_ARROW);
	else if (PtInRect(&boxes.topLeft, point) || PtInRect(&boxes.bottomRight, point))  return LoadCursor(NULL, IDC_SIZENWSE);
	else if (PtInRect(&boxes.bottomLeft, point) || PtInRect(&boxes.topRight, point))  return LoadCursor(NULL, IDC_SIZENESW);
	else if (PtInRect(&boxes.midLeft, point) || PtInRect(&boxes.midRight, point))	  return LoadCursor(NULL, IDC_SIZEWE);
	else if (PtInRect(&boxes.topMid, point) || PtInRect(&boxes.bottomMid, point))	  return LoadCursor(NULL, IDC_SIZENS);
	else if (PtInRect(&displayRectangle, point))									  return LoadCursor(NULL, IDC_SIZEALL);
	return LoadCursor(NULL, IDC_ARROW);
}

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParameter, LPARAM lParameter) {
	static BOOL drag = FALSE;
	static POINT previousPosition;
	static LONG *selectedXCorner = NULL;
	static LONG *selectedYCorner = NULL;

	RECT displayRectangle = RectangleNormalizeTruncate(selectionRectangle, screenRectangle);
	SelectionPoints anchors = SelectionPointsMake(displayRectangle);
	SelectionHitboxes boxes = SelectionHitboxesMake(anchors);
	boxes = SelectionHitboxesExtend(boxes, displayRectangle);
	static POINT point;
	GetCursorPos(&point);

	switch (message) {
		case WM_SHOWWINDOW: {
			BOOL windowShown = wParameter;
			if (!windowShown) showSelectionOutline = FALSE;
			return DefWindowProc(window, message, wParameter, lParameter);
		}


		case WM_ACTIVATE: {
			BOOL activationStatus = HIWORD(wParameter);
			if (activationStatus == WA_INACTIVE) ShowWindow(window, SW_HIDE);
			return DefWindowProc(window, message, wParameter, lParameter);
		}

		case WM_DISPLAYCHANGE:
			if (IsWindowVisible(window)) ShowWindow(window, SW_HIDE);

			SelectObject(memoryDeviceContext, previousMemoryBitmap);
			DeleteDC(memoryDeviceContext);
			DeleteObject(memoryBitmap);
			memoryDeviceContext = CreateCompatibleDC(screenDeviceContext);

			SCREEN_WIDTH = GetSystemMetrics(SM_CXSCREEN);
			SCREEN_HEIGHT = GetSystemMetrics(SM_CYSCREEN);
			screenRectangle = RectangleMake(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

			// Create screen compatible bitmap and associate it with the memory device context
			memoryBitmap = CreateCompatibleBitmap(screenDeviceContext, SCREEN_WIDTH, SCREEN_HEIGHT);
			HBITMAP previousMemoryBitmap = SelectObject(memoryDeviceContext, memoryBitmap);

			return 0;


		case WM_HOTKEY:
			if (!IsWindowVisible(window)) {
				selectionRectangle = (RECT){ 0 };
				lastSavedSelection = RectangleListInsertAfter(lastSavedSelection, selectionRectangle);
				// Transfer color data from screen to memory
				BOOL transferred = BitBlt(memoryDeviceContext, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, screenDeviceContext, 0, 0, SRCCOPY);
				if (GetForegroundWindow() != window) SetForegroundWindow(window);
				SetWindowPos(window, HWND_TOP, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SWP_SHOWWINDOW);
				// BringWindowToTop(window);
			}
			return 0;

		case WM_COMMAND:
			if (WindowOnShortcut(window, message, wParameter, lParameter) == 0) {
				RECT update = RectangleUpdateRegion(displayRectangle, selectionRectangle, screenRectangle, SELECTION_HITBOX_SIZE / 2);
				BOOL repaint = InvalidateRect(window, &update, TRUE);
				lastSavedSelection = RectangleListAdd(lastSavedSelection, selectionRectangle);
				return 0;
			}
			return 1;

		case WM_SETCURSOR: {
			// Update cursor based on position while left click is not held
			SHORT leftClick = GetAsyncKeyState(VK_LBUTTON) & 0x8000;
			if (!leftClick) SetCursor(WindowGetCursor(point, displayRectangle, boxes));
			// If left click is held and selection is not visible, show a default resize icon
			else if (leftClick && !RectangleHasArea(selectionRectangle)) SetCursor(LoadCursor(NULL, IDC_SIZENWSE));
			return TRUE;
		}

		case WM_LBUTTONDOWN: {
			BOOL cursorInSelection = PtInRect(&displayRectangle, point);
			int squareLength = RectangleMinSideLength(selectionRectangle);

			if (PtInRect(&boxes.topLeft, point)) {
				selectedXCorner = &selectionRectangle.left;
				selectedYCorner = &selectionRectangle.top;

				if (GetAsyncKeyState(VK_LSHIFT) & 0x8000) {
					selectionRectangle.left = selectionRectangle.right - squareLength;
					selectionRectangle.top = selectionRectangle.bottom - squareLength;
				}
			}
			else if (PtInRect(&boxes.bottomRight, point)) {
				selectedXCorner = &selectionRectangle.right;
				selectedYCorner = &selectionRectangle.bottom;

				if (GetAsyncKeyState(VK_LSHIFT) & 0x8000) {
					selectionRectangle.right = selectionRectangle.left + squareLength;
					selectionRectangle.bottom = selectionRectangle.top + squareLength;
				}
			}
			else if (PtInRect(&boxes.bottomLeft, point)) {
				selectedXCorner = &selectionRectangle.left;
				selectedYCorner = &selectionRectangle.bottom;

				if (GetAsyncKeyState(VK_LSHIFT) & 0x8000) {
					selectionRectangle.left = selectionRectangle.right - squareLength;
					selectionRectangle.bottom = selectionRectangle.top + squareLength;
				}
			}
			else if (PtInRect(&boxes.topRight, point)) {
				selectedXCorner = &selectionRectangle.right;
				selectedYCorner = &selectionRectangle.top;

				if (GetAsyncKeyState(VK_LSHIFT) & 0x8000) {
					selectionRectangle.right = selectionRectangle.left + squareLength;
					selectionRectangle.top = selectionRectangle.bottom - squareLength;
				}
			}
			else if (PtInRect(&boxes.midLeft, point)) {
				selectedXCorner = &selectionRectangle.left;
				selectedYCorner = NULL;

				if (GetAsyncKeyState(VK_LSHIFT) & 0x8000) selectionRectangle.left = selectionRectangle.right - RectangleHeight(selectionRectangle);
			}
			else if (PtInRect(&boxes.midRight, point)) {
				selectedXCorner = &selectionRectangle.right;
				selectedYCorner = NULL;

				if (GetAsyncKeyState(VK_LSHIFT) & 0x8000) selectionRectangle.right = selectionRectangle.left + RectangleHeight(selectionRectangle);
			}
			else if (PtInRect(&boxes.topMid, point)) {
				selectedXCorner = NULL;
				selectedYCorner = &selectionRectangle.top;

				if (GetAsyncKeyState(VK_LSHIFT) & 0x8000) selectionRectangle.top = selectionRectangle.bottom - RectangleWidth(selectionRectangle);
			}
			else if (PtInRect(&boxes.bottomMid, point)) {
				selectedXCorner = NULL;
				selectedYCorner = &selectionRectangle.bottom;

				if (GetAsyncKeyState(VK_LSHIFT) & 0x8000) selectionRectangle.bottom = selectionRectangle.top + RectangleWidth(selectionRectangle);
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

			RECT update = RectangleUpdateRegion(displayRectangle, selectionRectangle, screenRectangle, SELECTION_HITBOX_SIZE / 2);
			BOOL repaint = InvalidateRect(window, &update, TRUE);
			return 0;
		}

		case WM_MOUSEMOVE: {
			// https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-mousemove
			if ((wParameter & MK_LBUTTON) && !drag) {
				if (selectedXCorner) *selectedXCorner = point.x;
				if (selectedYCorner) *selectedYCorner = point.y;
				RECT update = RectangleUpdateRegion(displayRectangle, selectionRectangle, screenRectangle, SELECTION_HITBOX_SIZE / 2);
				BOOL repaint = InvalidateRect(window, &update, TRUE);
			}
			else if ((wParameter & MK_LBUTTON) && drag) {
				POINT difference = PositionSubtract(point, previousPosition);
				selectionRectangle = RectangleTranslate(selectionRectangle, difference);
				RECT update = RectangleUpdateRegion(displayRectangle, selectionRectangle, screenRectangle, SELECTION_HITBOX_SIZE / 2);
				BOOL repaint = InvalidateRect(window, &update, TRUE);
			}
			previousPosition = point;
			return 0;
		}

		case WM_LBUTTONUP: {
			// Normalize rectangle on release to ensure that the corner coordinates are consistent for subsequent rectangle transformations
			selectionRectangle = RectangleNormalizeTruncate(selectionRectangle, screenRectangle);

			// Track selection history in list
			lastSavedSelection = RectangleListAdd(lastSavedSelection, selectionRectangle);
			// assert(selections == RectangleListFirst(lastSavedSelection));

			// If selection is not visible when left click is released, show default cursor
			if (!RectangleHasArea(selectionRectangle)) SetCursor(LoadCursor(NULL, IDC_ARROW));
			drag = FALSE;

			RECT update = RectangleUpdateRegion(displayRectangle, selectionRectangle, screenRectangle, SELECTION_HITBOX_SIZE / 2);
			BOOL repaint = InvalidateRect(window, &update, TRUE);
		}


		case WM_PAINT: {
			PAINTSTRUCT paint;
			HDC client = BeginPaint(window, &paint);

			RECT update = paint.rcPaint;
			RECT sceneCoords = { .left = 0, .top = 0, .right = RectangleWidth(update), .bottom = RectangleHeight(update) };

			HDC scene = CreateCompatibleDC(client);
			HBITMAP sceneBitmap = CreateCompatibleBitmap(client, RectangleWidth(update), RectangleHeight(update));
			HBITMAP previousSceneBitmap = SelectObject(scene, sceneBitmap);
			HBRUSH backgroundColor = CreateSolidBrush(RGB(0, 0, 0));

			FillRect(scene, &sceneCoords, backgroundColor);
			BLENDFUNCTION blend = { 0 };
			blend.BlendOp = AC_SRC_OVER;
			blend.SourceConstantAlpha = 128;
			blend.AlphaFormat = AC_SRC_ALPHA;
			BOOL blended = GdiAlphaBlend(scene, 0, 0, RectangleWidth(update), RectangleHeight(update),
										 memoryDeviceContext, update.left, update.top, RectangleWidth(update), RectangleHeight(update), blend);

			if (RectangleHasArea(displayRectangle)) {
				// Display rectangle must be relative to the update region, not the client
				BitBlt(scene, (displayRectangle.left - update.left), (displayRectangle.top - update.top), displayRectangle.right - displayRectangle.left, displayRectangle.bottom - displayRectangle.top,
					   memoryDeviceContext, displayRectangle.left, displayRectangle.top, SRCCOPY);

				if (showSelectionOutline) {
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

			BitBlt(client, update.left, update.top, RectangleWidth(update), RectangleHeight(update), scene, 0, 0, SRCCOPY);

			// Clean up
			DeleteObject(backgroundColor);
			SelectObject(scene, previousSceneBitmap);
			DeleteDC(scene);
			DeleteObject(sceneBitmap);
			EndPaint(window, &paint);
			return 0;
		}

		case WM_DESTROY:
			// Free selection history
			RectangleListFree(RectangleListFirst(lastSavedSelection));
			PostQuitMessage(0);
			return 0;

		default:
			return DefWindowProc(window, message, wParameter, lParameter);
	}
}

void ConfigGetShortcut(ACCEL *shortcut, BYTE defaultMods, WORD defaultKey, DWORD windowCommand, wchar_t *configVariable) {
	wchar_t lineBuffer[MAX_PATH];
	DWORD charactersCopied = GetPrivateProfileStringW(L"keys", configVariable, NULL, lineBuffer, MAX_PATH, configPath);
	CharUpperW(lineBuffer);

	if (!charactersCopied) {
		*shortcut = (ACCEL){ .fVirt = FVIRTKEY | defaultMods, .key = defaultKey, .cmd = windowCommand };
		return;
	}

	*shortcut = (ACCEL) { .fVirt = FVIRTKEY, .key = 0, .cmd = windowCommand };

	int i = 0;
	while (lineBuffer[i]) {
		if (lineBuffer[i] == '+' || IsCharSpaceW(lineBuffer[i])) {
			i += 1;
		}
		else if (wcsncmp(&lineBuffer[i], SHIFT_STRING, wcslen(SHIFT_STRING)) == 0) {
			shortcut->fVirt |= FSHIFT;
			i += wcslen(SHIFT_STRING);
		}
		else if (wcsncmp(&lineBuffer[i], CTRL_STRING, wcslen(CTRL_STRING)) == 0) {
			shortcut->fVirt |= FCONTROL;
			i += wcslen(CTRL_STRING);
		}
		else if (wcsncmp(&lineBuffer[i], ALT_STRING, wcslen(ALT_STRING)) == 0) {
			shortcut->fVirt |= FALT;
			i += wcslen(ALT_STRING);
		}
		else if (wcsncmp(&lineBuffer[i], HEX_PREFIX, wcslen(HEX_PREFIX)) == 0) {
			int j = i + wcslen(HEX_PREFIX);
			while (lineBuffer[j]) {
				if ('0' <= lineBuffer[j] && lineBuffer[j] <= 'F') j += 1;
				else break;
			}
			int hexSuffixLength = j - (i + wcslen(HEX_PREFIX));
			if (hexSuffixLength != 2) {
				*shortcut = (ACCEL){ .fVirt = FVIRTKEY | defaultMods, .key = defaultKey, .cmd = windowCommand };
				MessageBoxW(NULL, L"Could not assign a key due to an invalid hex key code in the config. Code must be in range 0x01 to 0xFE. Default key was assigned instead.", L"Warning", MB_OK | MB_ICONWARNING);
				return;
			}
			wchar_t hexKeyCode[MAX_PATH];
			wcsncpy(hexKeyCode, &lineBuffer[i], wcslen(HEX_PREFIX) + hexSuffixLength + 1);
			int keyCode = 0;
			StrToIntExW(hexKeyCode, STIF_SUPPORT_HEX, &keyCode);
			shortcut->key = (WORD) keyCode;
			i += wcslen(HEX_PREFIX) + hexSuffixLength;
		}
		else {
			shortcut->key = lineBuffer[i];
			i += 1;
		}
	}
}

UINT ConfigShortcutModsToHotKeyMods(WORD shortcutMods) {
	UINT hotkeyMods = 0;
	hotkeyMods |= (shortcutMods & FSHIFT) ? MOD_SHIFT : 0;
	hotkeyMods |= (shortcutMods & FCONTROL) ? MOD_CONTROL : 0;
	hotkeyMods |= (shortcutMods & FALT) ? MOD_ALT : 0;
	return hotkeyMods;
}

BOOL ConfigLoad(window) {
	ACCEL screenCaptureShortcut = { 0 };
	ConfigGetShortcut(&screenCaptureShortcut, NULL, VK_SNAPSHOT, NULL, L"SCREEN_CAPTURE");

	// If shortcut table already exists, destroy it
	if (shortcutTable) {
		DestroyAcceleratorTable(shortcutTable);
		shortcutTable = NULL;
	}

	ACCEL shortcuts[NUM_SHORTCUTS];
	ConfigGetShortcut(&shortcuts[0], NULL, VK_ESCAPE, ID_CLOSE, L"CLOSE");
	ConfigGetShortcut(&shortcuts[1], NULL, 'F', ID_OUTLINE_SELECTION, L"OUTLINE_SELECTION");
	ConfigGetShortcut(&shortcuts[2], NULL, 'R', ID_RELOAD_CONFIG, L"RELOAD_CONFIG");
	ConfigGetShortcut(&shortcuts[3], FCONTROL, 'E', ID_OPEN_PAINT, L"OPEN_PAINT");
	ConfigGetShortcut(&shortcuts[4], FCONTROL, 'C', ID_COPY, L"COPY");
	ConfigGetShortcut(&shortcuts[5], FCONTROL, 'W', ID_DESELECT, L"DESELECT");
	ConfigGetShortcut(&shortcuts[6], FCONTROL, 'A', ID_SELECT_ALL, L"SELECT_ALL");
	ConfigGetShortcut(&shortcuts[7], FCONTROL, 'Z', ID_UNDO, L"UNDO");
	ConfigGetShortcut(&shortcuts[8], FCONTROL, 'Y', ID_REDO, L"REDO");
	ConfigGetShortcut(&shortcuts[9], NULL, '2', ID_UPSCALE, L"UPSCALE");
	ConfigGetShortcut(&shortcuts[10], NULL, '1', ID_DOWNSCALE, L"DOWNSCALE");
	ConfigGetShortcut(&shortcuts[11], FCONTROL, 'S', ID_SAVE, L"SAVE");
	ConfigGetShortcut(&shortcuts[12], NULL, VK_GRAVE, ID_OPEN_CONFIG, L"OPEN_CONFIG");

	assert(NUM_SHORTCUTS == ARRAY_LEN(shortcuts));

	shortcutTable = CreateAcceleratorTable(shortcuts, ARRAY_LEN(shortcuts));

	DWORD charactersCopied = GetPrivateProfileStringW(L"output", L"FILE_PATH", exeDirectory, screenshotDirectory, MAX_PATH, configPath);
	if (!charactersCopied) wcscpy(screenshotDirectory, exeDirectory);

	if (!DirectoryExists(screenshotDirectory)) {
		MessageBoxW(NULL, L"Save location could not be found. Check the FILE_PATH variable in your config.", NULL, MB_OK | MB_ICONERROR);
		return FALSE;
	}

	GetPrivateProfileStringW(L"output", L"FILE_PREFIX", L"Screenshot_", screenshotPrefix, MAX_PATH, configPath);

	UnregisterHotKey(window, ID_HOTKEY_SCREEN_CAPTURE);
	if (!RegisterHotKey(window, ID_HOTKEY_SCREEN_CAPTURE, ConfigShortcutModsToHotKeyMods(screenCaptureShortcut.fVirt), screenCaptureShortcut.key)) {
		MessageBoxW(window, L"Screen capture key could not be bound.", NULL, MB_OK | MB_ICONERROR);
		return FALSE;
	}

	return TRUE;
}

int WINAPI wWinMain(_In_ HINSTANCE appInstance, _In_opt_ HINSTANCE previousInstance, _In_ PWSTR commandLine, _In_ int visibility) {
	// https://stackoverflow.com/a/33531179/32242805
	// https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createmutexw
	HANDLE singleInstanceMutex = CreateMutex(NULL, TRUE, L"Single Instance Mutex for Screenshot Application");
	if (GetLastError()) {
		MessageBoxW(NULL, L"Screenshot application is already running.", L"Warning", MB_OK | MB_ICONWARNING);
		return 1;
	}

	if (!GetExeDirectory(exeDirectory) || !GetConfigPath(configPath)) {
		MessageBoxW(NULL, L"Could not find config path or directory where executable is running.", NULL, MB_OK | MB_ICONERROR);
		return 1;
	}

	SCREEN_WIDTH = GetSystemMetrics(SM_CXSCREEN);
	SCREEN_HEIGHT = GetSystemMetrics(SM_CYSCREEN);

	// "By convention, the right and bottom edges of the rectangle are normally considered exclusive."
	screenRectangle = RectangleMake(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

	screenDeviceContext = GetDC(SCREEN_HANDLE);
	memoryDeviceContext = CreateCompatibleDC(screenDeviceContext);

	// Create screen compatible bitmap and associate it with the memory device context
	memoryBitmap = CreateCompatibleBitmap(screenDeviceContext, SCREEN_WIDTH, SCREEN_HEIGHT);
	previousMemoryBitmap = SelectObject(memoryDeviceContext, memoryBitmap);

	WNDCLASS windowClass = { 0 };
	windowClass.hInstance = appInstance;
	windowClass.lpszClassName = L"Window Class";
	windowClass.lpfnWndProc = WindowProcedure;
	windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
	RegisterClass(&windowClass);

	HWND window = CreateWindow(windowClass.lpszClassName, L"", WS_POPUP,
				 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT,
				 NULL, NULL, appInstance, NULL);

	if (!ConfigLoad(window)) {
		// TODO: Clean up?
		return 1;
	}

	ShowWindow(window, SW_HIDE);

	InitializeCriticalSection(&criticalSection);

	MSG message;
	BOOL messageReturned;
	while ((messageReturned = GetMessage(&message, window, 0, 0)) != 0) {
		if (messageReturned == -1) {
			// TODO: Handle error and possibly exit
			break;
		}
		if (!TranslateAccelerator(window, shortcutTable, &message)) {
			TranslateMessage(&message);
			DispatchMessage(&message);
		}
	}

	// Clean up
	ReleaseDC(SCREEN_HANDLE, screenDeviceContext);
	SelectObject(memoryDeviceContext, previousMemoryBitmap);
	DeleteDC(memoryDeviceContext);
	DeleteObject(memoryBitmap);

	DestroyAcceleratorTable(shortcutTable);

	DeleteCriticalSection(&criticalSection);
	ReleaseMutex(singleInstanceMutex);
	CloseHandle(singleInstanceMutex);

	return 0;
}