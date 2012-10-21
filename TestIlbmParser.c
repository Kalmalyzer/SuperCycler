
#include "parseIlbm.h"

#include <stdio.h>

int main(int argc, char** argv)
{
	if (argc != 2)
	{
		printf("usage: TestIlbmParser <filename>\n");
		return 0;
	}

	Ilbm* ilbm = parseIlbm(argv[1]);
	
	if (ilbm)
		freeIlbm(ilbm);
	
	return 0;


}
