#ifndef POSITION_H
#define POSITION_H

#include <windows.h>

static POINT PositionSubtract(POINT p1, POINT p2) {
	POINT difference;
	difference.x = p1.x - p2.x;
	difference.y = p1.y - p2.y;
	return difference;
}

#endif // POSITION_H