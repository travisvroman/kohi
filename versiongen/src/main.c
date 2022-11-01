#include <stdio.h>
#include <time.h>
#include <stdlib.h>

int main(int argc, const char** argv) {
    if (argc < 2) {
        printf("Kohi Version Generator Utility\n    usage: 'versiongen <major> <minor>'\n    example: 'versiongen 1 3' generates something like '1.3.22278.12345'.");
        return 1;
    }

    int major = atoi(argv[1]);
    int minor = atoi(argv[2]);

    time_t timer;
    struct tm* tm_info;

    timer = time(0);
    tm_info = localtime(&timer);

    // MAJOR.MINOR.BUILD.REV
    // build = last 2 of year and day of year
    // rev = number of seconds since midnight
    int revision = (tm_info->tm_hour * 60 * 60) + (tm_info->tm_min * 60) + tm_info->tm_sec;
    printf("%d.%d.%02d%02d.%05d", major, minor, tm_info->tm_year % 100, tm_info->tm_yday, revision);

    return 0;
}