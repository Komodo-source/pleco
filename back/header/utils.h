// utils.h
#ifndef UTILS_H
#define UTILS_H

#include <windows.h>

// Lance un processus, lui envoie du texte sur stdin, récupère stdout
// Retourne 0 en cas de succès, -1 en cas d'erreur
int run_process_with_input(
    const char* executable,
    const char* input_text,
    char* output_buffer,
    DWORD output_buffer_size
);

#endif
