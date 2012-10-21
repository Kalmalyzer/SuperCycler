
#include "parseIff.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEBUG_ILBM_PARSER
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

void parseErrorCallback(const char* message)
{
	printf("Error: %s\n", message);
}

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

typedef struct
{
	Ilbm* ilbm;
	bool encounteredBMHD;
	uint compression;
	bool hasMaskPlane;

} ParseIlbmState;

ParseIlbmState parseIlbmState;

void freeIlbm(Ilbm* ilbm)
{
	if (ilbm->depth && ilbm->planes[0].data)
		free(ilbm->planes[0].data);
	free(ilbm);
}

uint decodeRLE(uint8_t* dest, uint8_t* src, uint destBytes)
{
	uint8_t* srcStart = src;
	uint8_t* destEnd = dest + destBytes;

#ifdef DEBUG_ILBM_PARSER_BITMAP_DECODE
	printf("Decoding RLE datastream: output %u bytes\n", destBytes);
#endif
	
	while (dest != destEnd)
	{
		int8_t count = *src++;
#ifdef DEBUG_ILBM_PARSER_BITMAP_DECODE
		printf("  count = %d\n", count);
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

uint skipRLE(uint8_t* src, uint destBytes)
{
	uint8_t* srcStart = src;

#ifdef DEBUG_ILBM_PARSER_BITMAP_DECODE
	printf("Skipping RLE datastream: output %u bytes\n", destBytes);
#endif

	while (destBytes)
	{
		int8_t count = *src++;
#ifdef DEBUG_ILBM_PARSER_BITMAP_DECODE
		printf("  count = %d\n", count);
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

bool handleBMHD(void* buffer, unsigned int size)
{
	if (size != sizeof BitMapHeader)
	{
		parseErrorCallback("Invalid BMHD size");
		return false;
	}

	BitMapHeader* header = (BitMapHeader*) buffer;
	
	parseIlbmState.encounteredBMHD = true;
	parseIlbmState.ilbm->width = header->w;
	parseIlbmState.ilbm->height = header->h;
	parseIlbmState.ilbm->depth = header->nPlanes;

	switch (header->compression)
	{
		case cmpNone:
		case cmpByteRun1:
			parseIlbmState.compression = header->compression;
			break;
		default:
			{
				char buf[1024];
				sprintf(buf, "Unknown compression type %u\n", header->compression);
				parseErrorCallback(buf);
				return false;
			}
	}

	parseIlbmState.hasMaskPlane = (header->masking == mskHasMask);
	
#ifdef DEBUG_ILBM_PARSER
	printf("Image dimensions: %ux%u pixels, %u bitplanes%s\n", parseIlbmState.ilbm->width, parseIlbmState.ilbm->height, parseIlbmState.ilbm->depth, (parseIlbmState.hasMaskPlane ? " (+ 1 mask bitplane)" : ""));
	printf("Image compression: %s\n", parseIlbmState.compression ? "RLE" : "None");
#endif

	return true;
}

bool handleCMAP(void* buffer, unsigned int size)
{
	Ilbm* ilbm = parseIlbmState.ilbm;
	
	if (size % 3)
	{
		parseErrorCallback("CMAP chunk size must be an even multiple of 3 bytes");
		return false;
	}
	
	ilbm->palette.numColors = size / 3;
	
#ifdef DEBUG_ILBM_PARSER
		printf("Found %u palette entries\n", ilbm->palette.numColors);
#endif

	uint8_t* source = (uint8_t*) buffer;
	for (uint color = 0; color < ilbm->palette.numColors; ++color)
	{
		ilbm->palette.colors[color] = (source[0] << 16) | (source[1] << 8) | (source[2]);
		source += 3;
	}
	
	return true;
}

bool handleBODY(void* buffer, unsigned int size)
{
	Ilbm* ilbm = parseIlbmState.ilbm;
	uint bytesPerRow = ((ilbm->width + 15) / 16) * 2;
	uint bytesPerPlane = bytesPerRow * ilbm->height;
	uint bytesToAllocate = bytesPerPlane * ilbm->depth;

#ifdef DEBUG_ILBM_PARSER
	printf("Allocating memory for %ux%ux%u planes\n", ilbm->width, ilbm->height, ilbm->depth);
#endif

	if (!(ilbm->planes[0].data = malloc(bytesToAllocate)))
	{
		char buf[1024];
		sprintf(buf, "Unable to allocate %u bytes\n", bytesToAllocate);
		parseErrorCallback(buf);
		return false;
	}

	for (uint planeIndex = 1; planeIndex < ilbm->depth; ++planeIndex)
		ilbm->planes[planeIndex].data = (void*) ((uint8_t*) ilbm->planes[0].data + planeIndex * bytesPerPlane);

#ifdef DEBUG_ILBM_PARSER
	printf("Decoding bitmap data\n");
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
			printf("Decoding row %u, plane %u\n", row, plane);
#endif
			switch (parseIlbmState.compression)
			{
				case cmpNone:
					memcpy(destPtr, sourcePtr, bytesPerRow);
					sourcePtr += bytesPerRow;
					break;
				case cmpByteRun1:
					sourcePtr += decodeRLE(destPtr, sourcePtr, bytesPerRow);
					break;
				default:
					parseErrorCallback("Compression method not implemented");
					return false;
			}
		}

		if (parseIlbmState.hasMaskPlane)
		{
#ifdef DEBUG_ILBM_PARSER_BITMAP_DECODE
			printf("Skipping over mask plane\n");
#endif
			switch (parseIlbmState.compression)
			{
				case cmpNone:
					sourcePtr += bytesPerRow;
					break;
				case cmpByteRun1:
					sourcePtr += skipRLE(sourcePtr, bytesPerRow);
					break;
					return false;
				default:
					parseErrorCallback("Compression method not implemented");
					return false;
			}
		}
		
		if (sourcePtr > sourcePtrEnd)
		{
			parseErrorCallback("Error during BODY decoding (source buffer overrun)");
			return false;
		}
	}
	
	if (sourcePtr != sourcePtrEnd)
	{
		parseErrorCallback("Error during BODY decoding (source buffer underrun/overrun)");
		return false;
	}

#ifdef DEBUG_ILBM_PARSER
	printf("Finished decoding bitmap data\n");
#endif
	return true;
}

void cleanup(void)
{
	if (parseIlbmState.ilbm)
	{
		freeIlbm(parseIlbmState.ilbm);
		parseIlbmState.ilbm = 0;
	}
}

Ilbm* parseIlbm(const char* fileName)
{
	static IffChunkHandler chunkHandlers[] = {
		{ ID_BMHD, handleBMHD },
		{ ID_CMAP, handleCMAP },
		{ ID_BODY, handleBODY },
		{ 0, 0 },
	};
	static IffParseRules parseRules = { parseErrorCallback, chunkHandlers };
	
	parseIlbmState.ilbm = malloc(sizeof Ilbm);
	memset(parseIlbmState.ilbm, 0, sizeof Ilbm);

	if (!parseIff(fileName, &parseRules))
	{
		cleanup();
		return 0;
	}
	
	return parseIlbmState.ilbm;
}

int main(int argc, char** argv)
{
	if (argc != 2)
	{
		printf("usage: parseIlbm <filename>\n");
		return 0;
	}

	parseIlbm(argv[1]);
	
	return 0;


}
