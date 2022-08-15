#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "inv.h"

int main(int argc, char *argv[])
{
	const char *progname = argv[0];
	const char *fn = argv[argc - 1];
	const char *dump = 0;
	struct inv *inv;
	bool isBinary = false;
	int width;
	int height;
	int i;
	
	/* show arguments */
	if (argc < 2)
	{
		fprintf(stderr, "args: %s invivo.inv\n", progname);
		fprintf(stderr, "  optional args:\n");
		fprintf(stderr, "    --binary WxH     : specifies input file is binary\n");
		fprintf(stderr, "                       data previously exported using\n");
		fprintf(stderr, "                       the --dump option\n");
		fprintf(stderr, "                       e.g. --binary 536x536\n");
		fprintf(stderr, "    --dump   out.bin : specifies output binary file\n");
		return -1;
	}
	
	/* parse arguments */
	for (i = 1; i < argc - 1; ++i)
	{
		const char *this = argv[i];
		const char *next = argv[i + 1];
		
		/* arguments start with -- */
		if (strlen(this) < 2)
			goto L_unknown;
		this += 2;
		
		if (!strcmp(this, "binary"))
		{
			if (sscanf(next, "%dx%d", &width, &height) != 2)
			{
				fprintf(stderr, "argument '%s %s' malformatted\n", this, next);
				return -1;
			}
			
			isBinary = true;
			
			i += 1;
		}
		else if (!strcmp(this, "dump"))
		{
			dump = next;
			
			i += 1;
		}
		else
		{
		L_unknown:
			fprintf(stderr, "unknown argument '%s'\n", this);
			return -1;
		}
	}
	
	/* load inv file */
	if (isBinary)
	{
		if (!(inv = inv_load_binary(fn, width, height)))
			return -1;
	}
	else
	{
		if (!(inv = inv_load(fn)))
			return -1;
	}
	
	/* dump inv file */
	if (dump && inv_dump(inv, dump))
		return -1;
	
	/* cleanup */
	inv_free(inv);
	return 0;
}
