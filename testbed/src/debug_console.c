#include "debug_console.h"

#include <core/console.h>
#include <containers/darray.h>

// TODO(travis): statically-defined state for now.
typedef struct debug_console_state {
    // Number of lines displayed at once.
    u32 line_display_count;
    // Number of lines offset from bottom of list.
    u32 line_offset;
    // darray
    const char** lines;

} debug_console_state;

static debug_console_state* state_ptr;

void debug_console_create() {
    if(!state_ptr) {
        state_ptr->line_display_count = 10;
        state_ptr->line_offset = 0;
        state_ptr->lines = darray_create(const char*);

        // TODO: update the text based on number of lines to display and 
        // the number of lines offset from the bottom. A UI Text object can
        // be used for display for now. Can worry about colour in a separate pass.
        // Not going to consider word wrap. 
        // NOTE: also should consider clipping rectangles and newlines.
    }
}