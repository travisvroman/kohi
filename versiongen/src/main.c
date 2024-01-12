#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void print_use(void) {
    printf(
        "Kohi Version Generator Utility\n"
        "   usage: 'versiongen -n|<filename>'\n "
        "   -n : Numeric-only mode that emits only numbers. Note that only a build and revision are generated.\n"
        "examples:\n"
        "   'versiongen version.txt' generates something like '1.3.0.22278-12345' (where the contents of version.txt are '1.3.0')\n"
        "    'versiongen -n' generates something like '2227812345'.");
}

// NOTE: This is intentionally kept to a small number to prevent injection of any kind.
#define MAX_VERSION_FILE_READ_SIZE 10

int main(int argc, const char** argv) {
    int b_numeric_mode = 0;
    const char* version_text_file = 0;

    // Account for null terminator
    char read_version[MAX_VERSION_FILE_READ_SIZE + 1] = {0};
    if (argc == 2) {
        if ((argv[1][0] == '-') && (argv[1][1] == 'n')) {
            b_numeric_mode = 1;
        } else {
            version_text_file = argv[1];
            // Read the text from the version file.
            FILE* f = fopen(version_text_file, "r");
            if (!f) {
                printf("Error opening verion file: %s. Using a default version of 0.0.0.", version_text_file);
                strncpy(read_version, "0.0.0", 7);
                read_version[6] = 0;
            } else {
                fseek(f, 0, SEEK_END);
                long fsize = ftell(f);
                // Clamp
                if (fsize > MAX_VERSION_FILE_READ_SIZE) {
                    fsize = MAX_VERSION_FILE_READ_SIZE;
                }
                fseek(f, 0, SEEK_SET);

                fread(read_version, fsize, 1, f);
                fclose(f);
                char* lastchar = &read_version[fsize - 2];
                if (*lastchar == '\n' || *lastchar == '\r') {
                    *lastchar = 0;
                } else {
                    read_version[fsize] = 0;
                }
            }
        }
    } else {
        print_use();
        return 1;
    }

    time_t timer;
    struct tm* tm_info;

    timer = time(0);
    tm_info = localtime(&timer);

    int revision = (tm_info->tm_hour * 60 * 60) + (tm_info->tm_min * 60) + tm_info->tm_sec;

    if (b_numeric_mode) {
        // BUILDREV
        // build = last 2 of year and day of year
        // rev = number of seconds since midnight
        printf("%02d%02d%05d", tm_info->tm_year % 100, tm_info->tm_yday, revision);
    } else {
        // MAJOR.MINOR.PATCH.BUILD-REV
        // build = last 2 of year and day of year
        // rev = number of seconds since midnight
        printf("%s.%02d%02d-%05d", read_version, tm_info->tm_year % 100, tm_info->tm_yday, revision);
    }
    return 0;
}
