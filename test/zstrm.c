#include <stdio.h>
#include <stdlib.h>
#include <zstrm.h>


uint8 buffer[4096];


intxx
rcallback(uint8* buffer, uintxx size, void* user)
{
	uintxx r;
	
	r = fread(buffer, 1, size, (FILE*) user);
	if (r != size) {
		if (ferror((FILE*) user)) {
			return -1;
		}
	}
	
	return r;
}

intxx
wcallback(uint8* buffer, uintxx size, void* user)
{
	uintxx r;
	
	r = fwrite(buffer, 1, size, (FILE*) user);
	if (r != size) {
		if (ferror((FILE*) user)) {
			return -1;
		}
	}
	return r;
}


bool
inflate(TZStrm* z, FILE* source, FILE* target)
{
	uintxx r;
	
	zstrm_setiofn(z, rcallback, source);
	do {
		r = zstrm_r(z, buffer, sizeof(buffer));
		
		if (fwrite(buffer, 1, r, target) != r || ferror(target)) {
			puts("Error: IO error while writing file");
			return 0;
		}
	} while (zstrm_eof(z) == 0);
	
	if (zstrm_geterror(z)) {
		puts("Error: zstream error");
		return 0;
	}
	
	fflush(target);
	if (ferror(target)) {
		puts("Error: IO error");
		return 0;
	}
	return 1;
}

bool
deflate(TZStrm* z, FILE* source, FILE* target)
{
	uintxx r;
	
	zstrm_setiofn(z, wcallback, target);
	do {
		r = fread(buffer, 1, sizeof(buffer), source);
		if (ferror(source)) {
			puts("Error while reading file");
			return 0;
		}
		
		zstrm_w(z, buffer, r);
		if (zstrm_geterror(z)) {
			puts("Error: zstream error");
			return 0;
		}
	} while (feof(source) == 0);
	zstrm_endstream(z);
	
	if (zstrm_geterror(z)) {
		puts("Error: zstream error");
		return 0;
	}
	
	fflush(target);
	if (ferror(target)) {
		puts("Error: IO error");
		return 0;
	}
	return 1;
}

void
showusage(void)
{
	puts("Usage:");
	puts("thisprogram <0|1|2|3|4|5|6|7|8|9> <input file> <compressed file>");
	puts("thisprogram <compressed file> <output file>");
	exit(0);
}

int
main(int argc, char* argv[])
{
	intxx level;
	char* lvend;
	FILE* source;
	FILE* target;
	TZStrm* z;
	
	if (argc != 3 && argc != 4) {
		showusage();
	}
	
	if (argc == 4) {
		lvend = argv[1];
		level = strtoll(argv[1], &lvend, 0);
		if (lvend == argv[1] || level < 0 || level > 9) {
			puts("Invalid compression level...");
			showusage();
		}
		
		source = fopen(argv[2], "rb");
		target = fopen(argv[3], "wb");
		z = zstrm_create(ZSTRM_WMODE + level, ZSTRM_GZIP);
	}
	else {
		source = fopen(argv[1], "rb");
		target = fopen(argv[2], "wb");
		z = zstrm_create(ZSTRM_RMODE, ZSTRM_AUTO);
	}
	
	if (source == NULL || target == NULL || z == NULL) {
		puts("IO error");
		if (source)
			fclose(source);
		if (target)
			fclose(target);
		
		zstrm_destroy(z);
		exit(0);
	}
	
	if (argc == 4) {
		deflate(z, source, target);
	}
	else {
		inflate(z, source, target);
	}
	
	zstrm_destroy(z);
	fclose(source);
	fclose(target);
	return 0;
}