
#include "parseIlbm.h"

#include <stdio.h>

void parseErrorCallback(const char* message)
{
	printf("Error: %s\n", message);
}

int main(int argc, char** argv)
{
	if (argc != 2)
	{
		printf("usage: TestIlbmParser <filename>\n");
		return 0;
	}

	Ilbm* ilbm = parseIlbm(argv[1], parseErrorCallback);
	
	if (ilbm)
		freeIlbm(ilbm);
	
	return 0;


}
