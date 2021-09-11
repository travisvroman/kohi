#pragma once

#include "defines.h"

// Holds a handle to a file.
typedef struct file_handle {
    // Opaque handle to internal file handle.
    void* handle;
    b8 is_valid;
} file_handle;

typedef enum file_modes {
    FILE_MODE_READ = 0x1,
    FILE_MODE_WRITE = 0x2
} file_modes;

/**
 * Checks if a file with the given path exists.
 * @param path The path of the file to be checked.
 * @returns True if exists; otherwise false.
 */
KAPI b8 filesystem_exists(const char* path);

/** 
 * Attempt to open file located at path.
 * @param path The path of the file to be opened.
 * @param mode Mode flags for the file when opened (read/write). See file_modes enum in filesystem.h.
 * @param binary Indicates if the file should be opened in binary mode.
 * @param out_handle A pointer to a file_handle structure which holds the handle information.
 * @returns True if opened successfully; otherwise false.
 */
KAPI b8 filesystem_open(const char* path, file_modes mode, b8 binary, file_handle* out_handle);

/** 
 * Closes the provided handle to a file.
 * @param handle A pointer to a file_handle structure which holds the handle to be closed.
 */
KAPI void filesystem_close(file_handle* handle);

/** 
 * Reads up to a newline or EOF. Allocates *line_buf, which must be freed by the caller.
 * @param handle A pointer to a file_handle structure.
 * @param line_buf A pointer to a character array which will be allocated and populated by this method.
 * @returns True if successful; otherwise false.
 */
KAPI b8 filesystem_read_line(file_handle* handle, char** line_buf);

/** 
 * Writes text to the provided file, appending a '\n' afterward.
 * @param handle A pointer to a file_handle structure.
 * @param text The text to be written.
 * @returns True if successful; otherwise false.
 */
KAPI b8 filesystem_write_line(file_handle* handle, const char* text);

/** 
 * Reads up to data_size bytes of data into out_bytes_read. 
 * Allocates *out_data, which must be freed by the caller.
 * @param handle A pointer to a file_handle structure.
 * @param data_size The number of bytes to read.
 * @param out_data A pointer to a block of memory to be populated by this method.
 * @param out_bytes_read A pointer to a number which will be populated with the number of bytes actually read from the file.
 * @returns True if successful; otherwise false.
 */
KAPI b8 filesystem_read(file_handle* handle, u64 data_size, void* out_data, u64* out_bytes_read);

/** 
 * Reads up to data_size bytes of data into out_bytes_read. 
 * Allocates *out_bytes, which must be freed by the caller.
 * @param handle A pointer to a file_handle structure.
 * @param out_bytes A pointer to a byte array which will be allocated and populated by this method.
 * @param out_bytes_read A pointer to a number which will be populated with the number of bytes actually read from the file.
 * @returns True if successful; otherwise false.
 */
KAPI b8 filesystem_read_all_bytes(file_handle* handle, u8** out_bytes, u64* out_bytes_read);

/** 
 * Writes provided data to the file.
 * @param handle A pointer to a file_handle structure.
 * @param data_size The size of the data in bytes.
 * @param data The data to be written.
 * @param out_bytes_written A pointer to a number which will be populated with the number of bytes actually written to the file.
 * @returns True if successful; otherwise false.
 */
KAPI b8 filesystem_write(file_handle* handle, u64 data_size, const void* data, u64* out_bytes_written);
