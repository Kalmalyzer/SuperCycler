
#ifndef ILBM_H
#define ILBM_H

#include "Types.h"
#include "ParseIff.h"

typedef struct
{
	uint numColors;
	uint32_t colors[256];
} IlbmPalette;

typedef struct
{
	void* data;
} IlbmPlane;

typedef struct
{
	uint low;
	uint high;
	uint16_t rate;
	bool reverse;
} IlbmColorRange;

enum { MaxIlbmColorRanges = 16 };
enum { MaxIlbmPlanes = 8 };

typedef struct 
{
	uint width;
	uint height;
	uint depth;
	IlbmPalette palette;
	uint bytesPerRow;
	IlbmPlane planes[MaxIlbmPlanes];
	uint numColorRanges;
	IlbmColorRange colorRanges[MaxIlbmColorRanges];
} Ilbm;

Ilbm* loadIffImage(const char* fileName, IffErrorFunc errorFunc);
void freeIlbm(Ilbm* ilbm);

#endif
