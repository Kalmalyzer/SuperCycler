
#ifndef PARSEILBM_H
#define PARSEILBM_H

#include "Types.h"

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
	uint width;
	uint height;
	uint depth;
	IlbmPalette palette;
	IlbmPlane planes[8];
} Ilbm;

Ilbm* parseIlbm(const char* fileName);
void freeIlbm(Ilbm* ilbm);

#endif
