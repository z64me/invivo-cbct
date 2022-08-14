#include <stdio.h>
#include "inv.h"

int main(int argc, char *argv[])
{
	const char *progname = argv[0];
	const char *fn = argv[1];
	struct inv *inv;
	
	/* show arguments */
	if (argc != 2)
	{
		fprintf(stderr, "args: %s invivo.inv\n", progname);
		return -1;
	}
	
	/* load inv file */
	if (!(inv = inv_load(fn)))
		return -1;
	
	/* cleanup */
	inv_free(inv);
	return 0;
}
