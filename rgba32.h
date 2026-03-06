#ifndef RGBA32_H
#define RGBA32_H

#include <stdint.h>

#pragma pack(push, 1)
typedef struct {
	uint8_t red;
	uint8_t green;
	uint8_t blue;
	uint8_t alpha;
} RGBA32;

typedef struct {
	uint8_t blue;
	uint8_t green;
	uint8_t red;
	uint8_t alpha;
} BGRA32;
#pragma pack(pop)

static uint32_t BGRA32toRGBA32(uint32_t bgraCode) {
	BGRA32 bgra = *((BGRA32 *)&bgraCode);
	RGBA32 rgba = { .red = bgra.red, .green = bgra.green, .blue = bgra.blue, .alpha = bgra.alpha };
	uint32_t rgbaCode = *((uint32_t *)&rgba);
	return rgbaCode;
}

#endif // RGBA32_H