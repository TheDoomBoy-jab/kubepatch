#include "shm_context.h"
#include <stdlib.h>

const char *shm_context_name(void) {
    const char *name = getenv("KUBEPATCH_SHM");
    return name && *name ? name : SHM_NAME;
}
