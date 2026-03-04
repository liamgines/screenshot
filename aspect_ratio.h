#ifndef ASPECT_RATIO_H
#define ASPECT_RATIO_H

#include <windows.h>

BOOL AspectRatioEqual(SIZE a, SIZE b) {
	return (a.cx == b.cx && a.cy == b.cy);
}

BOOL AspectRatioIsPositive(SIZE a) {
	return (a.cx > 0) && (a.cy > 0);
}

#endif // ASPECT_RATIO_H