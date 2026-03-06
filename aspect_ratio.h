#ifndef ASPECT_RATIO_H
#define ASPECT_RATIO_H

#include <windows.h>

static BOOL AspectRatioEqual(SIZE a, SIZE b) {
	return (a.cx == b.cx && a.cy == b.cy);
}

static BOOL AspectRatioIsPositive(SIZE a) {
	return (a.cx > 0) && (a.cy > 0);
}

#endif // ASPECT_RATIO_H