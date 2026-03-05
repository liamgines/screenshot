#ifndef RECTANGLE_H
#define RECTANGLE_H

#include <windows.h>
#include <stdlib.h>

#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define SWAP(TYPE, x, y) \
do {					 \
	TYPE temp;			 \
	temp = x;			 \
	x = y;				 \
	y = temp;			 \
} while (0)

static int GCD(int a, int b) {
	if (b == 0) return a;

	return GCD(b, a % b);
}

static RECT RectangleMake(LONG left, LONG top, LONG right, LONG bottom) {
	return (RECT) { .left = left, .top = top, .right = right, .bottom = bottom };
}

static LONG RectangleWidth(RECT r) {
	return r.right - r.left;
}

static LONG RectangleHeight(RECT r) {
	return r.bottom - r.top;
}

static LONG RectangleArea(RECT r) {
	LONG w = r.right - r.left;
	LONG h = r.bottom - r.top;
	return w * h;
}

static BOOL RectangleHasArea(RECT r) {
	if (RectangleArea(r) != 0) return TRUE;
	return FALSE;
}

static BOOL RectangleEqual(RECT a, RECT b) {
	return (a.left == b.left) && (a.top == b.top) && (a.right == b.right) && (a.bottom == b.bottom);
}

static RECT RectangleTranslate(RECT rectangle, POINT translation) {
	rectangle.left += translation.x;
	rectangle.top += translation.y;
	rectangle.right += translation.x;
	rectangle.bottom += translation.y;
	return rectangle;
}

static LONG *RectangleBottom(RECT *a) {
	if (a->bottom >= a->top) return &a->bottom;
	return &a->top;
}

static LONG *RectangleRight(RECT *a) {
	if (a->right >= a->left) return &a->right;
	return &a->left;
}

static RECT RectangleNormalize(RECT rectangle) {
	if (rectangle.right - rectangle.left < 0) SWAP(LONG, rectangle.right, rectangle.left);
	if (rectangle.bottom - rectangle.top < 0) SWAP(LONG, rectangle.bottom, rectangle.top);
	return rectangle;
}

// Expects a normalized rectangle
static RECT RectangleTruncate(RECT r, RECT bounds) {
	if (r.left < bounds.left) r.left = bounds.left;
	if (r.top < bounds.top) r.top = bounds.top;
	if (r.right > bounds.right) r.right = bounds.right;
	if (r.bottom > bounds.bottom) r.bottom = bounds.bottom;
	return r;
}

static RECT RectangleNormalizeTruncate(RECT r, RECT bounds) {
	return RectangleTruncate(RectangleNormalize(r), bounds);
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