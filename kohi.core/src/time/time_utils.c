#include "time_utils.h"

#include "math/kmath.h"
#include "strings/kstring.h"

const char* time_as_string_from_seconds(f32 total_seconds) {

    // Extract whole hours
    u32 hours = (u32)(total_seconds / 3600);
    total_seconds -= hours * 3600;

    // Extract whole minutes
    u32 minutes = (u32)(total_seconds / 60);
    total_seconds -= minutes * 60;

    // Separate seconds and fractional seconds
    u32 seconds = (u32)total_seconds;
    f32 fractional_seconds = total_seconds - seconds;

    return string_format("%02u:%02u:%02u.%02u", hours, minutes, seconds, (u32)(fractional_seconds * 100));
}

u64 milliseconds_from_seconds_f64(f64 seconds) {
    return (u64)(seconds * 1000);
}
