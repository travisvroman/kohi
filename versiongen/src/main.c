#include <stdio.h>
#include <time.h>
#include <stdlib.h>

void print_use() {
    printf("Kohi Version Generator Utility\n    usage: 'versiongen -n|<major> <minor>'\n    example: 'versiongen 1 3' generates something like '1.3.22278.12345', while 'versiongen -n' generates something like '2227812345'.");
}

int main(int argc, const char** argv) {
    int b_numeric_mode = 0;
    if (argc == 2) {
        b_numeric_mode = 1;
        if ((argv[1][0] != '-') || (argv[1][1] != 'n')) {
            print_use();
            return 1;
        }
    } else if (argc < 3) {
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
        // MAJOR.MINOR.BUILD.REV
        // build = last 2 of year and day of year
        // rev = number of seconds since midnight
        int major = atoi(argv[1]);
        int minor = atoi(argv[2]);
        printf("%d.%d.%02d%02d.%05d", major, minor, tm_info->tm_year % 100, tm_info->tm_yday, revision);
    }
    return 0;
}