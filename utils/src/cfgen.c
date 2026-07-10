/**
 * Kohi compile flags generator.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void print_use (void) {
	printf(
		"Kohi Compile Flags Generator Utility\n"
		"   usage: 'cfgen -outfile=<out_file_path> <args>'\n"
		"NOTE: All args passed must be surrounded in quotes if they contain spaces.\n");
}

int main (int argc, const char **argv) {
	char *out_file = 0;

	// Account for null terminator
	if (argc >= 3) {
		int len = strlen(argv[1]);
		out_file = malloc(len - 9 + 1);
		strncpy(out_file, argv[1] + 9, len - 9);
		out_file[len - 9] = 0;
		printf("out_file: '%s'\n", out_file);
	} else {
		print_use();
		return 1;
	}

	FILE *wf = fopen(out_file, "w");
	for (int i = 2; i < argc; ++i) {
		fwrite(argv[i], strlen(argv[i]), 1, wf);
		fwrite("\n", 1, 1, wf);
	}
	fclose(wf);
	return 0;
}
