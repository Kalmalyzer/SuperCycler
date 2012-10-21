
#include "parseIff.h"

#include <stdio.h>
#include <stdlib.h>

//#define DEBUG_IFF_PARSER

typedef struct
{
	const char* fileName;
	FILE* fileHandle;
	uint32_t compositeBytesLeft;
	void* chunkBuffer;
} IffParseContext;

typedef struct
{
	uint32_t compositeType;
	uint32_t compositeSize;
	uint32_t dataType;
} IffHeader;

typedef struct
{
	uint32_t id;
	uint32_t size;
} IffChunkHeader;

static bool validateIffHeader(const IffHeader* iffHeader)
{
	if (iffHeader->compositeType != ID_FORM)
		return false;
	if (iffHeader->compositeSize > 16*1024*1024 || iffHeader->compositeSize < 4)
		return false;
	if (iffHeader->dataType != ID_ILBM)
		return false;

	return true;
}

static void cleanup(IffParseContext* parseContext)
{
	if (parseContext->fileHandle)
		fclose(parseContext->fileHandle);
}

static bool validateIffChunkHeader(const IffChunkHeader* chunkHeader, unsigned int compositeBytesLeft)
{
	if (chunkHeader->size > compositeBytesLeft)
		return false;
		
	return true;
}

static bool readBytesFromStream(IffParseContext* parseContext, const IffParseRules* rules, void* buffer, size_t bytes)
{
	if (fread(buffer, bytes, 1, parseContext->fileHandle) != 1)
	{
		char buf[1024];
		sprintf(buf, "Unable to read %d bytes", (int) bytes);
		rules->errorFunc(buf);
		return false;
	}
	
	parseContext->compositeBytesLeft -= bytes;
	
	return true;
}

static bool processChunkHeader(IffParseContext* parseContext, const IffParseRules* rules, IffChunkHeader* chunkHeader)
{
#ifdef DEBUG_IFF_PARSER
	printf("DEBUG_IFF_PARSER: Reading IFF chunk header from file\n");
#endif

	if (parseContext->compositeBytesLeft < sizeof chunkHeader)
	{
		rules->errorFunc("Malformed IFF file");
		return false;
	}

	if (!readBytesFromStream(parseContext, rules, chunkHeader, sizeof *chunkHeader))
		return false;
	
#ifdef DEBUG_IFF_PARSER
	printf("DEBUG_IFF_PARSER: Encountered chunk %c%c%c%c, size %u\n", (char) (chunkHeader->id >> 24), (char) (chunkHeader->id >> 16), (char) (chunkHeader->id >> 8), (char) chunkHeader->id, chunkHeader->size);
#endif

	if (!validateIffChunkHeader(chunkHeader, parseContext->compositeBytesLeft))
	{
		char buf[1024];
		sprintf(buf, "Invalid IFF chunk header in file");
		rules->errorFunc(buf);
		return false;
	}

	return true;
}

static bool processChunkData(IffParseContext* parseContext, const IffParseRules* rules, const IffChunkHeader* chunkHeader)
{
	if (!(parseContext->chunkBuffer = malloc(chunkHeader->size)))
	{
		char buf[1024];
		sprintf(buf, "Unable to allocate %u bytes", chunkHeader->size);
		rules->errorFunc(buf);
		return false;
	}

#ifdef DEBUG_IFF_PARSER
	printf("DEBUG_IFF_PARSER: Reading chunk data from file\n");
#endif

	if (!readBytesFromStream(parseContext, rules, parseContext->chunkBuffer, chunkHeader->size))
		return false;
	
#ifdef DEBUG_IFF_PARSER
	printf("DEBUG_IFF_PARSER: Locating chunk handler\n");
#endif

	IffChunkHandler* chunkHandler;
	for (chunkHandler = rules->chunkHandlers; chunkHandler->id; chunkHandler++)
		if (chunkHandler->id == chunkHeader->id)
			break;

	if (chunkHandler->id)
	{
#ifdef DEBUG_IFF_PARSER
		printf("DEBUG_IFF_PARSER: Invoking chunk handler\n");
#endif
		bool result = chunkHandler->handlerFunc(rules->chunkHandlerState, parseContext->chunkBuffer, chunkHeader->size);
#ifdef DEBUG_IFF_PARSER
		printf("DEBUG_IFF_PARSER: Chunk handling %s\n", result ? "succeeded" : "failed");
#endif
		if (!result)
			return false;
	}
	else
	{
#ifdef DEBUG_IFF_PARSER
		printf("DEBUG_IFF_PARSER: No chunk handler found, chunk will be ignored\n");
#endif
	}
	
	free(parseContext->chunkBuffer);
	parseContext->chunkBuffer = 0;
	
	return true;
}

static bool processChunkPad(IffParseContext* parseContext, const IffParseRules* rules, const IffChunkHeader* chunkHeader)
{
	if ((chunkHeader->size & 1) && parseContext->compositeBytesLeft)
	{
#ifdef DEBUG_IFF_PARSER
	printf("DEBUG_IFF_PARSER: Skipping pad byte after chunk\n");
#endif

		char c;
		if (!readBytesFromStream(parseContext, rules, &c, sizeof c))
			return false;
	}
	
	return true;
}

static bool processChunks(IffParseContext* parseContext, const IffParseRules* rules)
{
	while (parseContext->compositeBytesLeft)
	{
		IffChunkHeader chunkHeader;
		
		if (!processChunkHeader(parseContext, rules, &chunkHeader)
			|| !processChunkData(parseContext, rules, &chunkHeader)
			|| !processChunkPad(parseContext, rules, &chunkHeader))
			return false;
	}
	
	return true;
}

bool parseIff(const char* fileName, const IffParseRules* rules)
{
	IffParseContext parseContext = { 0 };
	IffHeader iffHeader;
	
#ifdef DEBUG_IFF_PARSER
	printf("DEBUG_IFF_PARSER: Opening %s\n", fileName);
#endif

	parseContext.fileName = fileName;
	parseContext.fileHandle = fopen(fileName, "rb");

	if (!parseContext.fileHandle)
	{
		char buf[1024];
		sprintf(buf, "Unable to open file");
		rules->errorFunc(buf);
		return false;
	}

#ifdef DEBUG_IFF_PARSER
	printf("DEBUG_IFF_PARSER: Reading IFF header from file\n");
#endif

	if (fread(&iffHeader, sizeof iffHeader, 1, parseContext.fileHandle) != 1)
	{
		char buf[1024];
		sprintf(buf, "Unable to read %d bytes", (int) sizeof iffHeader);
		rules->errorFunc(buf);
		cleanup(&parseContext);
		return false;
	}

	if (!validateIffHeader(&iffHeader))
	{
		rules->errorFunc("Invalid IFF header");
		cleanup(&parseContext);
		return false;
	}

	parseContext.compositeBytesLeft = iffHeader.compositeSize - 4;

#ifdef DEBUG_IFF_PARSER
	printf("DEBUG_IFF_PARSER: Processing all chunks in file\n");
#endif

	bool result = processChunks(&parseContext, rules);
	
#ifdef DEBUG_IFF_PARSER
	printf("DEBUG_IFF_PARSER: Chunk processsing %s\n", result ? "completed successfully" : "failed");
#endif

#ifdef DEBUG_IFF_PARSER
	printf("DEBUG_IFF_PARSER: Cleaning up resources\n");
#endif

	cleanup(&parseContext);
	
	return result;
}
