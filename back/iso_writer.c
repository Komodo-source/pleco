// iso_writer.c
#include "header/iso_writer.h"
#include <windows.h>
#include <wincrypt.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "advapi32.lib")

// BLOCK_SIZE DOIT être au niveau global, pas dans la fonction
#define BLOCK_SIZE (4 * 1024 * 1024)

// ── Vérification SHA-256 ──────────────────────────────────────────────────

int verify_iso_sha256(const char* iso_path, const char* expected_hash) {
    HCRYPTPROV  hProv  = 0;
    HCRYPTHASH  hHash  = 0;
    HANDLE      hFile;
    BYTE        buffer[65536];
    DWORD       bytes_read;
    BYTE        hash_bytes[32];
    DWORD       hash_size = 32;
    char        hash_hex[65] = {0};
    int         result = 0;

    hFile = CreateFileA(iso_path, GENERIC_READ, FILE_SHARE_READ,
                        NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "[Erreur] Impossible d'ouvrir l'ISO : %s\n", iso_path);
        return 0;
    }

    CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT);
    CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash);

    while (ReadFile(hFile, buffer, sizeof(buffer), &bytes_read, NULL) && bytes_read > 0) {
        CryptHashData(hHash, buffer, bytes_read, 0);
    }

    CryptGetHashParam(hHash, HP_HASHVAL, hash_bytes, &hash_size, 0);

    for (int i = 0; i < 32; i++) {
        sprintf(hash_hex + i * 2, "%02x", hash_bytes[i]);
    }

    result = (_stricmp(hash_hex, expected_hash) == 0);

    if (!result) {
        fprintf(stderr, "[Erreur] Hash SHA-256 invalide !\n");
        fprintf(stderr, "  Attendu  : %s\n", expected_hash);
        fprintf(stderr, "  Calculé  : %s\n", hash_hex);
    } else {
        printf("[Pleco] Hash SHA-256 valide.\n");
    }

    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
    CloseHandle(hFile);
    return result;
}

// ── Écriture de l'ISO ─────────────────────────────────────────────────────

int write_iso_to_partition(
    const char* iso_path,
    const char* partition_path,
    progress_callback_t progress_cb
) {
    HANDLE hISO, hPartition;
    BYTE*  buffer;
    DWORD  bytes_read, bytes_written;
    LARGE_INTEGER iso_size;
    unsigned long long total_written = 0;

    iso_size.QuadPart = 0;

    // Ouvrir l'ISO en lecture
    hISO = CreateFileA(iso_path, GENERIC_READ, FILE_SHARE_READ,
                       NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (hISO == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "[Erreur] Impossible d'ouvrir l'ISO.\n");
        return -1;
    }

    GetFileSizeEx(hISO, &iso_size);

    // Ouvrir la partition en écriture brute
    hPartition = CreateFileA(
        partition_path,
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH,
        NULL
    );

    if (hPartition == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "[Erreur] Impossible d'ouvrir la partition %s. (Droits admin ?)\n",
                partition_path);
        CloseHandle(hISO);
        return -1;
    }

    // VirtualAlloc garantit l'alignement mémoire requis par FILE_FLAG_NO_BUFFERING
    buffer = (BYTE*)VirtualAlloc(NULL, BLOCK_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!buffer) {
        fprintf(stderr, "[Erreur] Allocation mémoire échouée.\n");
        CloseHandle(hISO);
        CloseHandle(hPartition);
        return -1;
    }

    printf("[Pleco] Écriture de l'ISO sur %s...\n", partition_path);

    while (ReadFile(hISO, buffer, BLOCK_SIZE, &bytes_read, NULL) && bytes_read > 0) {
        // Arrondir au multiple de 512 octets (requis par NO_BUFFERING)
        DWORD aligned = (bytes_read + 511) & ~511;
        if (aligned > bytes_read) {
            memset(buffer + bytes_read, 0, aligned - bytes_read);
        }

        if (!WriteFile(hPartition, buffer, aligned, &bytes_written, NULL)) {
            fprintf(stderr, "[Erreur] WriteFile échoué : %lu\n", GetLastError());
            break;
        }

        total_written += bytes_read;

        if (progress_cb) {
            progress_cb(total_written, (unsigned long long)iso_size.QuadPart);
        }
    }

    printf("\n[Pleco] ISO écrit : %llu Mo\n", total_written / (1024 * 1024));

    VirtualFree(buffer, 0, MEM_RELEASE);
    CloseHandle(hISO);
    CloseHandle(hPartition);
    return 0;
}
