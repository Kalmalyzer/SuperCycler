
#include "parseIff.h"

#include <stdio.h>

#define DEBUG_ILBM_PARSER


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
	bool encounteredBMHD;
	uint width;
	uint height;
	uint depth;
	uint compression;
	bool hasMaskPlane;

} ParseIlbmState;

ParseIlbmState parseIlbmState;

bool handleBMHD(void* buffer, unsigned int size)
{
	if (size != sizeof BitMapHeader)
	{
		parseErrorCallback("Invalid BMHD size");
		return false;
	}

	BitMapHeader* header = (BitMapHeader*) buffer;
	
	parseIlbmState.encounteredBMHD = true;
	parseIlbmState.width = header->w;
	parseIlbmState.height = header->h;
	parseIlbmState.depth = header->nPlanes;

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
	printf("Image dimensions: %ux%u pixels, %u bitplanes%s\n", parseIlbmState.width, parseIlbmState.height, parseIlbmState.depth, (parseIlbmState.hasMaskPlane ? " (+ 1 mask bitplane)" : ""));
	printf("Image compression: %s\n", parseIlbmState.compression ? "RLE" : "None");
#endif

	return true;
}

bool handleBODY(void* buffer, unsigned int size)
{

	return true;
}

bool parseIlbm(const char* fileName)
{
	static IffChunkHandler chunkHandlers[] = {
		{ ID_BMHD, handleBMHD },
		{ ID_BODY, handleBODY },
		{ 0, 0 },
	};
	static IffParseRules parseRules = { parseErrorCallback, chunkHandlers };

	return parseIff(fileName, &parseRules);
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
