// partitioning.c
#include "header/partitioning.h"
#include "utils.h"
#include <windows.h>
#include <stdio.h>

int create_temp_partition(unsigned int size_mb, char drive_letter) {
    char script[1024];
    char output[4096];

    snprintf(script, sizeof(script),
        "select disk 0\n"
        "create partition primary size=%u\n"
        "format fs=fat32 quick label=\"PLECO_TEMP\"\n"
        "assign letter=%c\n"
        "exit\n",
        size_mb, drive_letter
    );

    printf("[Pleco] Création de la partition (%u Mo, lettre %c:)...\n",
           size_mb, drive_letter);

    int result = run_process_with_input("diskpart", script, output, sizeof(output));

    if (result != 0) {
        fprintf(stderr, "[Erreur] diskpart a échoué :\n%s\n", output);
        return -1;
    }

    printf("[Pleco] Partition créée avec succès.\n");
    return 0;
}

int delete_partition(char drive_letter) {
    char script[512];
    char output[4096];

    snprintf(script, sizeof(script),
        "select disk 0\n"
        "select volume %c\n"
        "delete partition override\n"
        "exit\n",
        drive_letter
    );

    return run_process_with_input("diskpart", script, output, sizeof(output));
}

unsigned long long get_free_space_mb(void) {
    ULARGE_INTEGER free_bytes, total_bytes, total_free;

    if (GetDiskFreeSpaceExA("C:\\", &free_bytes, &total_bytes, &total_free)) {
        return free_bytes.QuadPart / (1024ULL * 1024ULL);
    }
    return 0;
}
