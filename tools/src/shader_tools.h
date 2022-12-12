#include <defines.h>

/*
TODO(travis): Need a way to recursively process shader files, looking
for #include statements and inserting the text in place along the way.

Editing a buffer _could_ work, but could easily wind up having to (manually) reallocate
a lot of memory along the way as it grows (especially if the includes are 
several layers deep).

A better way to do this might be to maintain a "master list of lines", and
simply remove the include line when found, then insert the other lines at
that same position. If using a darray, this could still have lots of reallocs,
but not near as many as it doubles in size every time it resizes.

The process, once complete, could then write out the lines to the resulting file.
This would be made a loooooot easier if, instead of plain old c-strings, we had a
structure that included a length and the pointer, and keep an array of those instead.

Maybe it's time for a proper string structure?
*/

/**
 * @brief 
 * 
 * @param source_file 
 * @param lines_darray A pointer to an array of cstrings.
 * @return i32 
 */
i32 process_source_file(const char* source_file);
// i32 process_source_file(const char* source_file, char*** lines_darray);

