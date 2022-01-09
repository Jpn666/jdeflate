#include <deflator.h>
#include <stdio.h>


static uint8 source[4096];
static uint8 target[4096];

int
compressfile(FILE* ihandler, FILE* ohandler)
{
	uintxx final;
	uintxx done;
	uintxx result;
	uintxx icount;
	uintxx ocount;
	TDeflator* state;
	
	state = deflator_create(9);
	if (state == NULL) {
		return 0;
	}
	
	final = done = 0;
	do {
		if (feof(ihandler) == 0) {
			icount = fread(source, 1, sizeof(source), ihandler);
			if (ferror(ihandler))
				goto L_ERROR;
			deflator_setsrc(state, source, icount);
		}
		else {
			final = 1;
		}
		
		do {
			deflator_settgt(state, target, sizeof(target));
			result = deflator_deflate(state, final);
			
			if ((ocount = deflator_tgtend(state))) {
				fwrite(target, 1, ocount, ohandler);
				if (ferror(ohandler))
					goto L_ERROR;
			}
		} while (result == DEFLT_TGTEXHSTD);
	} while (result == DEFLT_SRCEXHSTD);
	
	if (result == DEFLT_OK)
		done = 1;
L_ERROR:
	deflator_destroy(state);
	return done;
}

int
main(int argc, char* argv[])
{
	FILE* ihandler;
	FILE* ohandler;
	
	if (argc != 3) {
		puts("Usage: thisprogram <input file> <output>");
		return 0;
	}
	
	ihandler = fopen(argv[1], "rb");
	ohandler = fopen(argv[2], "wb");
	if (ihandler == NULL || ohandler == NULL) {
		if (ihandler)
			fclose(ihandler);
		if (ohandler)
			fclose(ohandler);
		return 0;
	}
	if (compressfile(ihandler, ohandler)) {
		puts("Done");
	}
	fclose(ihandler);
	fclose(ohandler);
	return 0;
}