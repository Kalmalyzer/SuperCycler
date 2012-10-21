
#include "parseIlbm.h"
#include "parseIff.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//#define DEBUG_ILBM_PARSER
//#define DEBUG_ILBM_PARSER_BITMAP_DECODE


typedef uint8_t Masking; 	/* Choice of masking technique. */

#define mskNone	0
#define mskHasMask	1
#define mskHasTransparentColor	2
#define mskLasso	3

typedef uint8_t Compression;	
	/* Choice of compression algorithm applied to the rows of all 
	 * source and mask planes. "cmpByteRun1" is the byte run encoding 
	 * described in Appendix C. Do not compress across rows! */
#define cmpNone	0
#define cmpByteRun1	1

typedef struct {
	uint16_t w, h;	/* raster width & height in pixels	*/
	int16_t  x, y;	/* pixel position for this image	*/
	uint8_t nPlanes;	/* # source bitplanes	*/
	Masking masking;
	Compression compression;
	uint8_t pad1;	/* unused; for consistency, put 0 here	*/
	uint16_t transparentColor;	/* transparent "color number" (sort of)	*/
	uint8_t xAspect, yAspect;	/* pixel aspect, a ratio width : height	*/
	uint16_t pageWidth, pageHeight;	/* source "page" size in pixels	*/
	} BitMapHeader;

typedef struct
{
	IffErrorFunc errorFunc;
	Ilbm* ilbm;
	bool encounteredBMHD;
	uint compression;
	bool hasMaskPlane;

} ParseIlbmState;

ParseIlbmState parseIlbmState;

static uint decodeRLE(uint8_t* dest, uint8_t* src, uint destBytes)
{
	uint8_t* srcStart = src;
	uint8_t* destEnd = dest + destBytes;

#ifdef DEBUG_ILBM_PARSER_BITMAP_DECODE
	printf("DEBUG_ILBM_PARSER_BITMAP_DECODE: Decoding RLE datastream: output %u bytes\n", destBytes);
#endif
	
	while (dest != destEnd)
	{
		int8_t count = *src++;
#ifdef DEBUG_ILBM_PARSER_BITMAP_DECODE
		printf("DEBUG_ILBM_PARSER_BITMAP_DECODE:  count = %d\n", count);
#endif
		if (count >= 0)
		{
			while (count-- != -1)
			{
				*dest++ = *src++;
			}
		}
		else if (count != -128)
		{
			uint8_t value = *src++;
			while (count++ != 1)
			{
				*dest++ = value;
			}
			
		}
		// count == 128 is a no-op
	}
	
	return (src - srcStart);
}

static uint skipRLE(uint8_t* src, uint destBytes)
{
	uint8_t* srcStart = src;

#ifdef DEBUG_ILBM_PARSER_BITMAP_DECODE
	printf("DEBUG_ILBM_PARSER_BITMAP_DECODE: Skipping RLE datastream: output %u bytes\n", destBytes);
#endif

	while (destBytes)
	{
		int8_t count = *src++;
#ifdef DEBUG_ILBM_PARSER_BITMAP_DECODE
		printf("DEBUG_ILBM_PARSER_BITMAP_DECODE:  count = %d\n", count);
#endif
		if (count >= 0)
		{
			src += (count + 1);
			destBytes -= (count + 1);
		}
		else if (count != -128)
		{
			src++;
			destBytes -= (-count + 1);
		}
		// count == 128 is a no-op
	}
	
	return (src - srcStart);
}

static bool handleBMHD(void* state_, void* buffer, unsigned int size)
{
	ParseIlbmState* state = (ParseIlbmState*) state_;
	if (size != sizeof BitMapHeader)
	{
		state->errorFunc("Invalid BMHD size");
		return false;
	}

	BitMapHeader* header = (BitMapHeader*) buffer;
	
	state->encounteredBMHD = true;
	Ilbm* ilbm = state->ilbm;
	ilbm->width = header->w;
	ilbm->height = header->h;
	ilbm->depth = header->nPlanes;

	switch (header->compression)
	{
		case cmpNone:
		case cmpByteRun1:
			state->compression = header->compression;
			break;
		default:
			{
				char buf[1024];
				sprintf(buf, "Unknown compression type %u\n", header->compression);
				state->errorFunc(buf);
				return false;
			}
	}

	state->hasMaskPlane = (header->masking == mskHasMask);
	
#ifdef DEBUG_ILBM_PARSER
	printf("DEBUG_ILBM_PARSER: Image dimensions: %ux%u pixels, %u bitplanes%s\n", ilbm->width, ilbm->height, ilbm->depth, (state->hasMaskPlane ? " (+ 1 mask bitplane)" : ""));
	printf("DEBUG_ILBM_PARSER: Image compression: %s\n", state->compression ? "RLE" : "None");
#endif

	return true;
}

static bool handleCMAP(void* state_, void* buffer, unsigned int size)
{
	ParseIlbmState* state = (ParseIlbmState*) state_;
	Ilbm* ilbm = state->ilbm;
	
	if (size % 3)
	{
		state->errorFunc("CMAP chunk size must be an even multiple of 3 bytes");
		return false;
	}
	
	ilbm->palette.numColors = size / 3;
	
#ifdef DEBUG_ILBM_PARSER
		printf("DEBUG_ILBM_PARSER: Found %u palette entries\n", ilbm->palette.numColors);
#endif

	uint8_t* source = (uint8_t*) buffer;
	for (uint color = 0; color < ilbm->palette.numColors; ++color)
	{
		ilbm->palette.colors[color] = (source[0] << 16) | (source[1] << 8) | (source[2]);
		source += 3;
	}
	
	return true;
}

static bool handleBODY(void* state_, void* buffer, unsigned int size)
{
	ParseIlbmState* state = (ParseIlbmState*) state_;
	Ilbm* ilbm = state->ilbm;
	uint bytesPerRow = ((ilbm->width + 15) / 16) * 2;
	uint bytesPerPlane = bytesPerRow * ilbm->height;
	uint bytesToAllocate = bytesPerPlane * ilbm->depth;

	if (!state->encounteredBMHD)
	{
		state->errorFunc("Unable to decode BODY before BMHD has been handled");
		return false;
	}
	
#ifdef DEBUG_ILBM_PARSER
	printf("DEBUG_ILBM_PARSER: Allocating memory for %ux%ux%u planes\n", ilbm->width, ilbm->height, ilbm->depth);
#endif

	if (!(ilbm->planes[0].data = malloc(bytesToAllocate)))
	{
		char buf[1024];
		sprintf(buf, "Unable to allocate %u bytes\n", bytesToAllocate);
		state->errorFunc(buf);
		return false;
	}

	for (uint planeIndex = 1; planeIndex < ilbm->depth; ++planeIndex)
		ilbm->planes[planeIndex].data = (void*) ((uint8_t*) ilbm->planes[0].data + planeIndex * bytesPerPlane);

#ifdef DEBUG_ILBM_PARSER
	printf("DEBUG_ILBM_PARSER: Decoding bitmap data\n");
#endif

	uint8_t* sourcePtr = (uint8_t*) buffer;
	uint8_t* sourcePtrEnd = sourcePtr + size;
	for (uint row = 0; row < ilbm->height; ++row)
	{
		for (uint plane = 0; plane < ilbm->depth; ++plane)
		{
			uint8_t* destPtr = (uint8_t*) ilbm->planes[plane].data + row * bytesPerRow;
			uint8_t* destPtrEnd = destPtr + bytesPerRow;

#ifdef DEBUG_ILBM_PARSER_BITMAP_DECODE
			printf("DEBUG_ILBM_PARSER: Decoding row %u, plane %u\n", row, plane);
#endif
			switch (state->compression)
			{
				case cmpNone:
					memcpy(destPtr, sourcePtr, bytesPerRow);
					sourcePtr += bytesPerRow;
					break;
				case cmpByteRun1:
					sourcePtr += decodeRLE(destPtr, sourcePtr, bytesPerRow);
					break;
				default:
					state->errorFunc("Compression method not implemented");
					return false;
			}
		}

		if (state->hasMaskPlane)
		{
#ifdef DEBUG_ILBM_PARSER_BITMAP_DECODE
			printf("DEBUG_ILBM_PARSER: Skipping over mask plane\n");
#endif
			switch (state->compression)
			{
				case cmpNone:
					sourcePtr += bytesPerRow;
					break;
				case cmpByteRun1:
					sourcePtr += skipRLE(sourcePtr, bytesPerRow);
					break;
					return false;
				default:
					state->errorFunc("Compression method not implemented");
					return false;
			}
		}
		
		if (sourcePtr > sourcePtrEnd)
		{
			state->errorFunc("Error during BODY decoding (source buffer overrun)");
			return false;
		}
	}
	
	if (sourcePtr != sourcePtrEnd)
	{
		state->errorFunc("Error during BODY decoding (source buffer underrun/overrun)");
		return false;
	}

#ifdef DEBUG_ILBM_PARSER
	printf("DEBUG_ILBM_PARSER: Finished decoding bitmap data\n");
#endif
	return true;
}

static void cleanup(ParseIlbmState* state)
{
	if (state->ilbm)
		freeIlbm(state->ilbm);
}

Ilbm* parseIlbm(const char* fileName, IffErrorFunc errorFunc)
{
	ParseIlbmState parseIlbmState = { 0 };

	IffChunkHandler chunkHandlers[] = {
		{ ID_BMHD, handleBMHD },
		{ ID_CMAP, handleCMAP },
		{ ID_BODY, handleBODY },
		{ 0, 0 },
	};
	IffParseRules parseRules;
	parseRules.errorFunc = errorFunc;
	parseRules.chunkHandlers = chunkHandlers;
	parseRules.chunkHandlerState = &parseIlbmState;

	parseIlbmState.errorFunc = errorFunc;
	parseIlbmState.ilbm = malloc(sizeof Ilbm);
	memset(parseIlbmState.ilbm, 0, sizeof Ilbm);

	if (!parseIff(fileName, &parseRules))
	{
		cleanup(&parseIlbmState);
		return 0;
	}
	
	return parseIlbmState.ilbm;
}

void freeIlbm(Ilbm* ilbm)
{
	if (ilbm->depth && ilbm->planes[0].data)
		free(ilbm->planes[0].data);
	free(ilbm);
}
