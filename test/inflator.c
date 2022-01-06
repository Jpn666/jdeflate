#include <inflator.h>
#include <stdio.h>


static uint8 source[4096];
static uint8 target[4096];

int
decompressfile(FILE* ihandler, FILE* ohandler)
{
	uintxx final;
	uintxx done;
	uintxx result;
	uintxx icount;
	uintxx ocount;
	TInflator* state;
	
	state = inflator_create();
	if (state == NULL) {
		return 0;
	}
	
	final = done = 0;
	do {
		if (feof(ihandler) == 0) {
			icount = fread(source, 1, sizeof(source), ihandler);
			if (ferror(ihandler))
				goto L_ERROR;
			inflator_setsrc(state, source, icount);
		}
		else {
			final = 1;
		}
		
		do {
			inflator_settgt(state, target, sizeof(target));
			result = inflator_inflate(state, final);
			
			if ((ocount = inflator_tgtend(state))) {
				fwrite(target, 1, ocount, ohandler);
				if (ferror(ohandler))
					goto L_ERROR;
			}
		} while (result == INFLT_TGTEXHSTD);
	} while (result == INFLT_SRCEXHSTD);
	
	if (result == INFLT_OK)
		done = 1;
L_ERROR:
	inflator_destroy(state);
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
	if (decompressfile(ihandler, ohandler)) {
		puts("Done");
	}
	fclose(ihandler);
	fclose(ohandler);
	return 0;
}