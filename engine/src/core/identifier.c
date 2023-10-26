#include "identifier.h"

#include <time.h>

#include "containers/darray.h"
#include "core/logger.h"
#include "math/mtwister.h"

static b8 generator_created = false;
static mtrand_state generator;

identifier identifier_create() {
    if (!generator_created) {
        generator = mtrand_create(time(0));
        generator_created = true;
    }
    identifier id;
    id.uniqueid = mtrand_generate(&generator);
    return id;
}

identifier identifier_from_u64(u64 uniqueid) {
    identifier id;
    id.uniqueid = uniqueid;
    return id;
}
