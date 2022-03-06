#include "kmath.h"
#include "platform/platform.h"

#include <math.h>
#include <stdlib.h>

static b8 rand_seeded = false;

typedef union {
    f32 elements[3];
    union{
        struct{ f32 x,y,z; };
        struct{ f32 r,g,b; };
        struct{ f32 s,t,p; };
        struct{ f32 u,v,w; };
    };
} test3;

void test() {
    test3 t;
    t.x = 1.0f;
    t.g = 2.0f;
}

/**
 * Note that these are here in order to prevent having to import the
 * entire <math.h> everywhere.
 */
f32 ksin(f32 x) {
    return sinf(x);
}

f32 kcos(f32 x) {
    return cosf(x);
}

f32 ktan(f32 x) {
    return tanf(x);
}

f32 kacos(f32 x) {
    return acosf(x);
}

f32 ksqrt(f32 x) {
    return sqrtf(x);
}

f32 kabs(f32 x) {
    return fabsf(x);
}

i32 krandom() {
    if (!rand_seeded) {
        srand((u32)platform_get_absolute_time());
        rand_seeded = true;
    }
    return rand();
}

i32 krandom_in_range(i32 min, i32 max) {
    if (!rand_seeded) {
        srand((u32)platform_get_absolute_time());
        rand_seeded = true;
    }
    return (rand() % (max - min + 1)) + min;
}

f32 fkrandom() {
    return (float)krandom() / (f32)RAND_MAX;
}

f32 fkrandom_in_range(f32 min, f32 max) {
    return min + ((float)krandom() / ((f32)RAND_MAX / (max - min)));
}
