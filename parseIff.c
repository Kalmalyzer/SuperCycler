
#include <stdio.h>
#include "Types.h"

typedef bool (*IffChunkHandlerFunc)(void);

typedef void (*IffErrorFunc)(const char* message);

typedef struct
{
	uint32_t id;
	IffChunkHandlerFunc handlerFunc;
} IffChunkHandler;

typedef struct
{
	IffErrorFunc errorFunc;
	IffChunkHandler* chunkHandlers;
} IffParseRules;

typedef struct
{
	const char* fileName;
	FILE* fileHandle;
} IffParseContext;



void parseIff(const char* fileName, IffParseRules* rules)
{
	IffParseContext parseContext = { 0 };
	parseContext.fileName = fileName;
	parseContext.fileHandle = fopen(fileName, "rb");

	if (!parseContext.fileHandle)
	{
		char buf[1024];
		sprintf("Unable to open %s", fileName);
		rules->errorFunc(fileName);
		return;
	}

	printf("done\n");
	
	fclose(parseContext.fileHandle);
}

void parseErrorCallback(const char* message)
{
	printf("Error: %s\n", message);
}


int main(int argc, char** argv)
{
	static IffChunkHandler chunkHandlers[] = { { 0 } };
	static IffParseRules parseRules = { parseErrorCallback, chunkHandlers };

	if (argc != 2)
	{
		printf("usage: parseiff <filename>\n");
		return 0;
	}
	
	parseIff(argv[1], &parseRules);
	
	return 0;
}