#ifndef RECTANGLE_H
#define RECTANGLE_H

#include <windows.h>
#include <stdlib.h>

#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define SWAP(type, x, y) \
do {					 \
	type temp;			 \
	temp = x;			 \
	x = y;				 \
	y = temp;			 \
} while (0)

static int GCD(int x, int y) {
	if (y == 0) return x;

	return GCD(y, x % y);
}

static RECT RectangleMake(LONG left, LONG top, LONG right, LONG bottom) {
	return (RECT) { .left = left, .top = top, .right = right, .bottom = bottom };
}

static LONG RectangleWidth(RECT a) {
	return a.right - a.left;
}

static LONG RectangleHeight(RECT a) {
	return a.bottom - a.top;
}

static LONG RectangleArea(RECT a) {
	LONG w = a.right - a.left;
	LONG h = a.bottom - a.top;
	return w * h;
}

static BOOL RectangleHasArea(RECT a) {
	if (RectangleArea(a) != 0) return TRUE;
	return FALSE;
}

static BOOL RectangleEqual(RECT a, RECT b) {
	return (a.left == b.left) && (a.top == b.top) && (a.right == b.right) && (a.bottom == b.bottom);
}

static RECT RectangleTranslate(RECT a, POINT translation) {
	a.left += translation.x;
	a.top += translation.y;
	a.right += translation.x;
	a.bottom += translation.y;
	return a;
}

static LONG *RectangleBottom(RECT *a) {
	if (a->bottom >= a->top) return &a->bottom;
	return &a->top;
}

static LONG *RectangleRight(RECT *a) {
	if (a->right >= a->left) return &a->right;
	return &a->left;
}

static RECT RectangleNormalize(RECT a) {
	if (a.right - a.left < 0) SWAP(LONG, a.right, a.left);
	if (a.bottom - a.top < 0) SWAP(LONG, a.bottom, a.top);
	return a;
}

// Expects a normalized rectangle
static RECT RectangleTruncate(RECT a, RECT bounds) {
	if (a.left < bounds.left) a.left = bounds.left;
	if (a.top < bounds.top) a.top = bounds.top;
	if (a.right > bounds.right) a.right = bounds.right;
	if (a.bottom > bounds.bottom) a.bottom = bounds.bottom;
	return a;
}

static RECT RectangleNormalizeTruncate(RECT a, RECT bounds) {
	return RectangleTruncate(RectangleNormalize(a), bounds);
}

static BOOL RectangleOutOfBounds(RECT a, RECT bounds) {
	a = RectangleNormalize(a);
	return !RectangleEqual(a, RectangleTruncate(a, bounds));
}

static RECT RectangleUpdateRegion(RECT before, RECT after, RECT bounds, int padding) {
	before = RectangleNormalizeTruncate(before, bounds);
	after = RectangleNormalizeTruncate(after, bounds);
	return (RECT) {
		.left = MIN(before.left, after.left) - padding,
		.top = MIN(before.top, after.top) - padding,
		.right = MAX(before.right, after.right) + padding,
		.bottom = MAX(before.bottom, after.bottom) + padding
	};
}

static RECT RectangleToSquare(RECT a) {
	LONG length = MAX(RectangleWidth(a), RectangleHeight(a));
	return (RECT) {
		.left = a.left,
		.top = a.top,
		.right = a.left + length,
		.bottom = a.top + length
	};
}

static SIZE RectangleAspectRatio(RECT a) {
	int w = RectangleWidth(a);
	int h = RectangleHeight(a);
	int gcd = GCD(w, h);

	SIZE aspectRatio = { 0 };
	if (gcd) {
		aspectRatio.cx = abs(w / gcd);
		aspectRatio.cy = abs(h / gcd);
	}
	return aspectRatio;
}

static int RectangleMinSideLength(RECT a) {
	return MIN(RectangleWidth(a), RectangleHeight(a));
}

#endif // RECTANGLE_H