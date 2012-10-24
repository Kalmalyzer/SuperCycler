
#include "Ilbm.h"
#include "parseIff.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//#define DEBUG_IFF_IMAGE_PARSER
//#define DEBUG_IFF_IMAGE_PARSER_BITMAP_DECODE


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

	
typedef struct {
	uint16_t pad1;      /* reserved for future use; store 0 here    */
	uint16_t rate;      /* color cycle rate                         */
	uint16_t flags;     /* see below                                */
	uint8_t  low, high; /* lower and upper color registers selected */
	} CRange;

#define RNG_ACTIVE  1
#define RNG_REVERSE 2

typedef enum
{
	PixelFormat_Unknown,
	PixelFormat_Ilbm,
	PixelFormat_Pbm,
} PixelFormat;
	
typedef struct
{
	IffErrorFunc errorFunc;
	Ilbm* ilbm;
	bool encounteredBMHD;
	bool encounteredBODY;
	uint compression;
	bool hasMaskPlane;
	PixelFormat pixelFormat;
	uint8_t* pbmRowBuffer;

} LoadIffImageState;

LoadIffImageState loadIffImageState;

static uint decodeRLE(uint8_t* dest, uint8_t* src, uint destBytes)
{
	uint8_t* srcStart = src;
	uint8_t* destEnd = dest + destBytes;

#ifdef DEBUG_IFF_IMAGE_PARSER_BITMAP_DECODE
	printf("DEBUG_IFF_IMAGE_PARSER_BITMAP_DECODE: Decoding RLE datastream: output %u bytes\n", destBytes);
#endif
	
	while (dest != destEnd)
	{
		int8_t count = *src++;
#ifdef DEBUG_IFF_IMAGE_PARSER_BITMAP_DECODE
		printf("DEBUG_IFF_IMAGE_PARSER_BITMAP_DECODE:  count = %d\n", count);
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

#ifdef DEBUG_IFF_IMAGE_PARSER_BITMAP_DECODE
	printf("DEBUG_IFF_IMAGE_PARSER_BITMAP_DECODE: Skipping RLE datastream: output %u bytes\n", destBytes);
#endif

	while (destBytes)
	{
		int8_t count = *src++;
#ifdef DEBUG_IFF_IMAGE_PARSER_BITMAP_DECODE
		printf("DEBUG_IFF_IMAGE_PARSER_BITMAP_DECODE:  count = %d\n", count);
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

static bool handleILBM(void* state_, void* buffer, unsigned int size)
{
	LoadIffImageState* state = (LoadIffImageState*) state_;
	state->pixelFormat = PixelFormat_Ilbm;
	return true;
}

static bool handlePBM(void* state_, void* buffer, unsigned int size)
{
	LoadIffImageState* state = (LoadIffImageState*) state_;
	state->pixelFormat = PixelFormat_Pbm;
	return true;
}

static bool handleBMHD(void* state_, void* buffer, unsigned int size)
{
	LoadIffImageState* state = (LoadIffImageState*) state_;
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
	ilbm->bytesPerRow = ((header->w + 15) / 16) * 2;

	switch (header->compression)
	{
		case cmpNone:
		case cmpByteRun1:
			state->compression = header->compression;
			break;
		default:
			{
				char buf[1024];
				sprintf(buf, "Unknown compression type %u", header->compression);
				state->errorFunc(buf);
				return false;
			}
	}

	state->hasMaskPlane = (header->masking == mskHasMask);

#ifdef DEBUG_IFF_IMAGE_PARSER
	printf("DEBUG_IFF_IMAGE_PARSER: Image dimensions: %ux%u pixels, %u bits per pixel%s\n", ilbm->width, ilbm->height, ilbm->depth, (state->hasMaskPlane ? " (+ 1 mask bitplane)" : ""));
	printf("DEBUG_IFF_IMAGE_PARSER: Image compression: %s\n", state->compression ? "RLE" : "None");
#endif

	if (ilbm->depth > MaxIlbmPlanes)
	{
		char buf[1024];
		sprintf(buf, "Parser does not support more than %u bits per pixel", (uint) MaxIlbmPlanes);
		state->errorFunc(buf);
		return false;
	}

	return true;
}

static bool handleCMAP(void* state_, void* buffer, unsigned int size)
{
	LoadIffImageState* state = (LoadIffImageState*) state_;
	Ilbm* ilbm = state->ilbm;
	
	if (size % 3)
	{
		state->errorFunc("CMAP chunk size must be an even multiple of 3 bytes");
		return false;
	}
	
	ilbm->palette.numColors = size / 3;
	
#ifdef DEBUG_IFF_IMAGE_PARSER
		printf("DEBUG_IFF_IMAGE_PARSER: Found %u palette entries\n", ilbm->palette.numColors);
#endif

	uint8_t* source = (uint8_t*) buffer;
	for (uint color = 0; color < ilbm->palette.numColors; ++color)
	{
		ilbm->palette.colors[color] = (source[0] << 16) | (source[1] << 8) | (source[2]);
		source += 3;
	}
	
	return true;
}

static bool handleCRNG(void* state_, void* buffer, unsigned int size)
{
	LoadIffImageState* state = (LoadIffImageState*) state_;
	Ilbm* ilbm = state->ilbm;
	
	if (size != sizeof CRange)
	{
		char buf[1024];
		sprintf(buf, "CRNG chunk must be %u bytes", (uint) sizeof CRange);
		state->errorFunc(buf);
		return false;
	}


	CRange* sourceRange = (CRange*) buffer;
	
	if (!(sourceRange->flags & RNG_ACTIVE))
	{
#ifdef DEBUG_IFF_IMAGE_PARSER
		printf("DEBUG_IFF_IMAGE_PARSER: Ignoring inactive color range\n");
#endif
		return true;
	}
	
	if (ilbm->numColorRanges == MaxIlbmColorRanges)
	{
		char buf[1024];
		sprintf(buf, "Parser supports at most %u active color ranges in a file", (uint) MaxIlbmColorRanges);
		state->errorFunc(buf);
		return false;
	}

	IlbmColorRange* destRange = &ilbm->colorRanges[ilbm->numColorRanges];

	destRange->low = (uint) sourceRange->low;
	destRange->high = (uint) sourceRange->high;
	destRange->rate = sourceRange->rate;
	destRange->reverse = (sourceRange->flags & RNG_REVERSE) ? true : false;
	
#ifdef DEBUG_IFF_IMAGE_PARSER
	printf("DEBUG_IFF_IMAGE_PARSER: Color range from %u to %u, rate %u%s\n", destRange->low, destRange->high, destRange->rate, destRange->reverse ? ", reverse" : "");
#endif

	ilbm->numColorRanges++;
	return true;
}

static bool handleBODY(void* state_, void* buffer, unsigned int size)
{
	LoadIffImageState* state = (LoadIffImageState*) state_;
	Ilbm* ilbm = state->ilbm;
	uint bytesPerRow = ilbm->bytesPerRow;
	uint bytesPerPlane = bytesPerRow * ilbm->height;
	uint bytesToAllocate = bytesPerPlane * ilbm->depth;

	if (!state->encounteredBMHD)
	{
		state->errorFunc("Unable to decode BODY before BMHD has been handled");
		return false;
	}
	
	if (state->encounteredBODY)
	{
#ifdef DEBUG_IFF_IMAGE_PARSER
		printf("DEBUG_IFF_IMAGE_PARSER: Ignoring multiple BODYs\n");
#endif
		return true;
	}

	state->encounteredBODY = true;
	
	if (state->pixelFormat == PixelFormat_Pbm && state->hasMaskPlane)
	{
		state->errorFunc("PBM format parser doesn't support mask plane");
		return false;
	}
	
#ifdef DEBUG_IFF_IMAGE_PARSER
	printf("DEBUG_IFF_IMAGE_PARSER: Allocating memory for %ux%ux%u planes\n", ilbm->width, ilbm->height, ilbm->depth);
#endif

	if (!(ilbm->planes[0].data = malloc(bytesToAllocate)))
	{
		char buf[1024];
		sprintf(buf, "Unable to allocate %u bytes", bytesToAllocate);
		state->errorFunc(buf);
		return false;
	}

	for (uint planeIndex = 1; planeIndex < ilbm->depth; ++planeIndex)
		ilbm->planes[planeIndex].data = (void*) ((uint8_t*) ilbm->planes[0].data + planeIndex * bytesPerPlane);

#ifdef DEBUG_IFF_IMAGE_PARSER
	printf("DEBUG_IFF_IMAGE_PARSER: Decoding bitmap data\n");
#endif

	if (state->pixelFormat == PixelFormat_Pbm)
	{
		uint rowBufferBytes = (ilbm->width + 31) & ~31;
		if (!(state->pbmRowBuffer = malloc(rowBufferBytes)))
		{
			char buf[1024];
			sprintf(buf, "Unable to allocate %u bytes", rowBufferBytes);
			state->errorFunc(buf);
			return false;
		}
		
		memset(state->pbmRowBuffer, 0, rowBufferBytes);
	}

	uint8_t* sourcePtr = (uint8_t*) buffer;
	uint8_t* sourcePtrEnd = sourcePtr + size;
	for (uint row = 0; row < ilbm->height; ++row)
	{
		switch (state->pixelFormat)
		{
			case PixelFormat_Ilbm:
			{
				for (uint plane = 0; plane < ilbm->depth; ++plane)
				{
					uint8_t* destPtr = (uint8_t*) ilbm->planes[plane].data + row * bytesPerRow;
					uint8_t* destPtrEnd = destPtr + bytesPerRow;

#ifdef DEBUG_IFF_IMAGE_PARSER_BITMAP_DECODE
					printf("DEBUG_IFF_IMAGE_PARSER: Decoding row %u, plane %u\n", row, plane);
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
#ifdef DEBUG_IFF_IMAGE_PARSER_BITMAP_DECODE
					printf("DEBUG_IFF_IMAGE_PARSER: Skipping over mask plane\n");
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
				break;
			}
			case PixelFormat_Pbm:
			{
#ifdef DEBUG_IFF_IMAGE_PARSER_BITMAP_DECODE
				printf("DEBUG_IFF_IMAGE_PARSER: Decoding row %u\n", row);
#endif
				switch (state->compression)
				{
					case cmpNone:
						memcpy(state->pbmRowBuffer, sourcePtr, ilbm->width);
						sourcePtr += ilbm->width;
						break;
					case cmpByteRun1:
						sourcePtr += decodeRLE(state->pbmRowBuffer, sourcePtr, ilbm->width);
						break;
					default:
						state->errorFunc("Compression method not implemented");
						return false;
				}
#ifdef DEBUG_IFF_IMAGE_PARSER_BITMAP_DECODE
				printf("DEBUG_IFF_IMAGE_PARSER: C2P converting row %u\n", row);
#endif
				uint16_t planeData[8] = { 0 };
				
				for (uint offset = 0; offset < ilbm->width; offset += sizeof planeData)
				{
					uint bytesToCopy = ilbm->width - offset;
					if (bytesToCopy > sizeof planeData)
						bytesToCopy = sizeof planeData;
					
					memcpy(planeData, &state->pbmRowBuffer[offset], bytesToCopy);
					uint bytesToClear = sizeof planeData - bytesToCopy;
					if (bytesToClear)
						memset(&state->pbmRowBuffer[offset + bytesToCopy], 0, bytesToClear);
					
#define MERGE16(a, b, temp, shift, mask) \
	temp = ((b >> shift) ^ a) & mask; \
	a ^= temp; \
	b ^= (temp << shift);

					uint16_t temp;
					MERGE16(planeData[0], planeData[4], temp, 8, 0x00ff);
					MERGE16(planeData[1], planeData[5], temp, 8, 0x00ff);
					MERGE16(planeData[2], planeData[6], temp, 8, 0x00ff);
					MERGE16(planeData[3], planeData[7], temp, 8, 0x00ff);
					MERGE16(planeData[0], planeData[2], temp, 4, 0x0f0f);
					MERGE16(planeData[1], planeData[3], temp, 4, 0x0f0f);
					MERGE16(planeData[4], planeData[6], temp, 4, 0x0f0f);
					MERGE16(planeData[5], planeData[7], temp, 4, 0x0f0f);
					MERGE16(planeData[0], planeData[1], temp, 2, 0x3333);
					MERGE16(planeData[2], planeData[3], temp, 2, 0x3333);
					MERGE16(planeData[4], planeData[5], temp, 2, 0x3333);
					MERGE16(planeData[6], planeData[7], temp, 2, 0x3333);
					MERGE16(planeData[0], planeData[4], temp, 1, 0x5555);
					MERGE16(planeData[1], planeData[5], temp, 1, 0x5555);
					MERGE16(planeData[2], planeData[6], temp, 1, 0x5555);
					MERGE16(planeData[3], planeData[7], temp, 1, 0x5555);

#undef MERGE16

					uint16_t shuffledPlaneData[8];
					
					shuffledPlaneData[7] = planeData[0];
					shuffledPlaneData[5] = planeData[1];
					shuffledPlaneData[3] = planeData[2];
					shuffledPlaneData[1] = planeData[3];
					shuffledPlaneData[6] = planeData[4];
					shuffledPlaneData[4] = planeData[5];
					shuffledPlaneData[2] = planeData[6];
					shuffledPlaneData[0] = planeData[7];

					for (uint plane = 0; plane < ilbm->depth; ++plane)
					{
						uint16_t* destPtr = (uint16_t*) ((uint8_t*) ilbm->planes[plane].data + row * bytesPerRow + (offset >> 3));
						*destPtr = shuffledPlaneData[plane];
					}

				}
				
				break;
			}
			default:
			{
				char buf[1024];
				sprintf(buf, "Unsupported pixelFormat %d", (int) state->pixelFormat);
				state->errorFunc(buf);
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

#ifdef DEBUG_IFF_IMAGE_PARSER
	printf("DEBUG_IFF_IMAGE_PARSER: Finished decoding bitmap data\n");
#endif
	return true;
}

static void cleanup(LoadIffImageState* state)
{
	if (state->ilbm)
		freeIlbm(state->ilbm);
		
	if (state->pbmRowBuffer)
		free(state->pbmRowBuffer);
}

Ilbm* loadIffImage(const char* fileName, IffErrorFunc errorFunc)
{
	LoadIffImageState loadIffImageState = { 0 };

	IffChunkHandler chunkHandlers[] = {
		{ ID_ILBM, handleILBM },
		{ ID_PBM,  handlePBM },
		{ ID_BMHD, handleBMHD },
		{ ID_CMAP, handleCMAP },
		{ ID_CRNG, handleCRNG },
		{ ID_BODY, handleBODY },
		{ 0, 0 },
	};
	IffParseRules parseRules;
	parseRules.errorFunc = errorFunc;
	parseRules.chunkHandlers = chunkHandlers;
	parseRules.chunkHandlerState = &loadIffImageState;

	loadIffImageState.errorFunc = errorFunc;
	loadIffImageState.ilbm = malloc(sizeof Ilbm);
	memset(loadIffImageState.ilbm, 0, sizeof Ilbm);

	if (!parseIff(fileName, &parseRules))
	{
		cleanup(&loadIffImageState);
		return 0;
	}
	else
	{
		Ilbm* ilbm = loadIffImageState.ilbm;
		loadIffImageState.ilbm = 0;
		cleanup(&loadIffImageState);
		return ilbm;
	}
}

void freeIlbm(Ilbm* ilbm)
{
	if (ilbm->depth && ilbm->planes[0].data)
		free(ilbm->planes[0].data);
	free(ilbm);
}
