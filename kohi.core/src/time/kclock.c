#include "time/kclock.h"

#include "platform/platform.h"

void kclock_update(kclock* clock) {
    if (clock->start_time != 0) {
        clock->elapsed = platform_get_absolute_time() - clock->start_time;
    }
}

void kclock_start(kclock* clock) {
    clock->start_time = platform_get_absolute_time();
    clock->elapsed = 0;
}

void kclock_stop(kclock* clock) {
    clock->start_time = 0;
}