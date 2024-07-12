#include "identifiers/identifier.h"

#include <time.h>

#include "containers/darray.h"
#include "logger.h"
#include "math/mtwister.h"

static b8 generator_created = false;
static mtrand_state generator;

identifier identifier_create(void) {
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

b8 identifiers_equal(identifier a, identifier b) {
    return a.uniqueid == b.uniqueid;
}
