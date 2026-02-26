// iso_writer.c
#include "header/iso_writer.h"
#include <windows.h>
#include <wincrypt.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "header/utils.h"

#pragma comment(lib, "advapi32.lib")

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
        fprintf(stderr, "  Calcule  : %s\n", hash_hex);
    } else {
        printf("[Pleco] Hash SHA-256 valide.\n");
    }

    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
    CloseHandle(hFile);
    return result;
}

// ── Écriture de l'ISO sur la partition (UEFI) ─────────────────────────────
// Approche : monter l'ISO via PowerShell, copier les fichiers avec robocopy,
// puis vérifier la présence du binaire EFI avant de démonter.
// La partition dest doit être FAT32 (créée par create_temp_partition).

int write_iso_to_partition(
    const char* iso_path,
    char drive_letter,
    progress_callback_t progress_cb
) {
    char output[4096];
    char ps_cmd[2048];
    char iso_drive = 0;

    if (progress_cb) progress_cb(0, 100);

    // 1. Monter l'ISO et récupérer la lettre de lecteur assignée
    printf("[Pleco] Montage de l'ISO via PowerShell...\n");
    snprintf(ps_cmd, sizeof(ps_cmd),
        "powershell -NoProfile -Command \""
        "$r = Mount-DiskImage -ImagePath '%s' -PassThru; "
        "$d = ($r | Get-Volume).DriveLetter; "
        "Write-Host $d\"",
        iso_path);

    run_process_with_input(ps_cmd, NULL, output, sizeof(output));

    // Parser la première lettre alphabétique dans la sortie
    for (int i = 0; output[i]; i++) {
        if (isalpha((unsigned char)output[i])) {
            iso_drive = (char)toupper((unsigned char)output[i]);
            break;
        }
    }

    if (!iso_drive) {
        fprintf(stderr,
            "[Erreur] Impossible de monter l'ISO ou de lire sa lettre de lecteur.\n"
            "         Sortie PowerShell : %s\n", output);
        return -1;
    }
    printf("[Pleco] ISO monte sur %c:\n", iso_drive);

    if (progress_cb) progress_cb(10, 100);

    // 2. Copier tous les fichiers de l'ISO vers la partition FAT32
    // /E  : sous-dossiers (y compris vides)
    // /NFL /NDL /NJH /NJS : sortie silencieuse
    // /R:1 /W:1 : 1 seule tentative en cas d'erreur
    printf("[Pleco] Copie des fichiers ISO vers %c:...\n", drive_letter);
    char copy_cmd[128];
    snprintf(copy_cmd, sizeof(copy_cmd),
        "robocopy %c:\\ %c:\\ /E /NFL /NDL /NJH /NJS /R:1 /W:1",
        iso_drive, drive_letter);

    // robocopy retourne 1 quand des fichiers ont ete copies — pas une erreur
    run_process_with_input(copy_cmd, NULL, output, sizeof(output));

    if (progress_cb) progress_cb(90, 100);

    // 3. Vérifier que le binaire EFI de boot est présent
    char efi_path[64];
    snprintf(efi_path, sizeof(efi_path), "%c:\\EFI\\boot\\bootx64.efi", drive_letter);
    if (GetFileAttributesA(efi_path) == INVALID_FILE_ATTRIBUTES) {
        fprintf(stderr,
            "[Erreur] Binaire EFI introuvable apres la copie : %s\n"
            "         L'ISO n'est peut-etre pas un ISO Linux UEFI (bootx64.efi manquant).\n",
            efi_path);
        snprintf(ps_cmd, sizeof(ps_cmd),
            "powershell -NoProfile -Command \""
            "Dismount-DiskImage -ImagePath '%s'\"",
            iso_path);
        run_process_with_input(ps_cmd, NULL, output, sizeof(output));
        return -1;
    }
    printf("[Pleco] Binaire EFI present : %s\n", efi_path);

    // 4. Démonter l'ISO
    snprintf(ps_cmd, sizeof(ps_cmd),
        "powershell -NoProfile -Command \""
        "Dismount-DiskImage -ImagePath '%s'\"",
        iso_path);
    run_process_with_input(ps_cmd, NULL, output, sizeof(output));
    printf("[Pleco] ISO demonte.\n");

    if (progress_cb) progress_cb(100, 100);
    return 0;
}
