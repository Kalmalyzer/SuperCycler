
#include "parseIff.h"

#include <stdio.h>

void parseErrorCallback(const char* message)
{
	printf("Error: %s\n", message);
}

bool handleBMHD(void)
{
	return true;
}

bool handleBODY(void)
{
	return true;
}

int main(int argc, char** argv)
{
	static IffChunkHandler chunkHandlers[] = {
		{ ID_BMHD, handleBMHD },
		{ ID_BODY, handleBODY },
		{ 0, 0 },
	};
	static IffParseRules parseRules = { parseErrorCallback, chunkHandlers };

	if (argc != 2)
	{
		printf("usage: TestIffParser <filename>\n");
		return 0;
	}
	
	parseIff(argv[1], &parseRules);
	
	return 0;
}
