#ifndef POSITION_H
#define POSITION_H

#include <windows.h>

static POINT PositionSubtract(POINT a, POINT b) {
	POINT difference;
	difference.x = a.x - b.x;
	difference.y = a.y - b.y;
	return difference;
}

#endif // POSITION_H