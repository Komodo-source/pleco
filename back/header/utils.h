#ifndef UTILS_H
#define UTILS_H

#include <windows.h>

int run_process_with_input(
    const char* executable,
    const char* input_text,
    char* output_buffer,
    DWORD output_buffer_size
);

#endif
