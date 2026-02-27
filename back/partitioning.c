// partitioning.c
#include "header/partitioning.h"
#include "header/utils.h"
#include <windows.h>
#include <stdio.h>

int create_temp_partition(unsigned int size_mb, char drive_letter) {
    char script[1024];
    char output[8192];

    // Sur disque GPT (Windows 11 utilise toujours GPT) :
    // - "efi" crée une partition de type EFI System Partition (ESP)
    // - format fs=fat32 est obligatoire pour une ESP
    // - L'UUID GPT EFI = c12a7328-f81f-11d2-ba4b-00a0c93ec93b est assigné
    //   automatiquement par "create partition efi"
    snprintf(script, sizeof(script),
        "select disk 0\n"
        "create partition efi size=%u\n"
        "format fs=fat32 quick label=\"PLECO_TEMP\"\n"
        "assign letter=%c\n"
        "exit\n",
        size_mb, drive_letter
    );

    printf("[Pleco] Creation de la partition EFI (%u Mo, lettre %c:)...\n",
           size_mb, drive_letter);

    run_process_with_input("diskpart", script, output, sizeof(output));
    printf("[Debug] Sortie diskpart :\n%s\n", output);

    Sleep(3000);

    // Vérification réelle : le volume existe-t-il ?
    char volume_path[8];
    snprintf(volume_path, sizeof(volume_path), "%c:\\", drive_letter);

    if (GetFileAttributesA(volume_path) == INVALID_FILE_ATTRIBUTES) {
        fprintf(stderr, "[Erreur] La partition %c: n'existe pas apres diskpart.\n",
                drive_letter);
        fprintf(stderr, "  1. Espace insuffisant -> shrink desired=15000\n");
        fprintf(stderr, "  2. Sortie diskpart :\n%s\n", output);
        return -1;
    }

    // Vérification en écriture
    char test_file[32];
    snprintf(test_file, sizeof(test_file), "%c:\\pleco_test.tmp", drive_letter);
    HANDLE hTest = CreateFileA(test_file, GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hTest == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "[Erreur] Partition %c: non accessible en ecriture (code %lu).\n",
                drive_letter, GetLastError());
        return -1;
    }
    CloseHandle(hTest);
    DeleteFileA(test_file);

    printf("[Pleco] Partition %c: creee et verifiee.\n", drive_letter);
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

    run_process_with_input("diskpart", script, output, sizeof(output));
    Sleep(1000);

    char volume_path[8];
    snprintf(volume_path, sizeof(volume_path), "%c:\\", drive_letter);
    if (GetFileAttributesA(volume_path) == INVALID_FILE_ATTRIBUTES) {
        printf("[Pleco] Partition %c: supprimee.\n", drive_letter);
        return 0;
    }

    fprintf(stderr, "[Attention] Impossible de supprimer la partition %c:.\n",
            drive_letter);
    return -1;
}

unsigned long long get_free_space_mb(void) {
    ULARGE_INTEGER free_bytes, total_bytes, total_free;
    if (GetDiskFreeSpaceExA("C:\\", &free_bytes, &total_bytes, &total_free)) {
        return free_bytes.QuadPart / (1024ULL * 1024ULL);
    }
    return 0;
}
