#include "kassert.h"

#include <signal.h>
#include <stdlib.h>

void kdebug_break() {
#if defined(_WIN32) || defined(_WIN64)
    __debugbreak();
#elif defined(__linux__) || defined(__APPLE__)
    raise(SIGTRAP);
#else
    abort(); // Fallback for unsupported platforms
#endif
}