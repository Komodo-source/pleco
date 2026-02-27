// iso_writer.c
#include "header/iso_writer.h"
#include "header/utils.h"
#include <windows.h>
#include <wincrypt.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#pragma comment(lib, "advapi32.lib")

// ── Recherche récursive de bootx64.efi ───────────────────────────────────

static int find_efi_binary(const char* dir, const char* rel,
                            char* out_rel_path, int out_size) {
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*", dir);

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;

    do {
        if (fd.cFileName[0] == '.') continue;

        char full[MAX_PATH], rel2[MAX_PATH];
        snprintf(full, sizeof(full), "%s\\%s", dir, fd.cFileName);
        snprintf(rel2, sizeof(rel2), "%s\\%s", rel, fd.cFileName);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (find_efi_binary(full, rel2, out_rel_path, out_size)) {
                FindClose(h);
                return 1;
            }
        } else if (_stricmp(fd.cFileName, "bootx64.efi") == 0) {
            strncpy(out_rel_path, rel2, out_size - 1);
            out_rel_path[out_size - 1] = '\0';
            FindClose(h);
            return 1;
        }
    } while (FindNextFileA(h, &fd));

    FindClose(h);
    return 0;
}

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

// ── Écriture de l'ISO (montage PowerShell + robocopy) ────────────────────

int write_iso_to_partition(
    const char* iso_path,
    char        drive_letter,
    progress_callback_t progress_cb,
    char*       out_efi_path,
    int         efi_path_size
) {
    char output[4096];
    char ps_cmd[2048];
    char iso_drive = 0;

    if (out_efi_path && efi_path_size > 0) out_efi_path[0] = '\0';
    if (progress_cb) progress_cb(0, 100);

    // ── Étape 1 : Monter l'ISO ────────────────────────────────────────────
    // On utilise des guillemets simples PowerShell autour du chemin ISO
    // pour éviter les problèmes d'échappement avec les backslashes
    printf("[Pleco] Montage de l'ISO via PowerShell...\n");

    // Convertir les backslashes en slashes pour PowerShell
    char iso_ps_path[MAX_PATH];
    strncpy(iso_ps_path, iso_path, sizeof(iso_ps_path) - 1);
    iso_ps_path[sizeof(iso_ps_path) - 1] = '\0';
    for (int i = 0; iso_ps_path[i]; i++) {
        if (iso_ps_path[i] == '/') iso_ps_path[i] = '\\';
    }

    snprintf(ps_cmd, sizeof(ps_cmd),
        "powershell -NoProfile -NonInteractive -Command "
        "\"$r = Mount-DiskImage -ImagePath '%s' -PassThru; "
        "$d = ($r | Get-Volume).DriveLetter; "
        "if ($d) { Write-Host $d } else { exit 1 }\"",
        iso_ps_path);

    if (run_process_with_input(ps_cmd, NULL, output, sizeof(output)) != 0) {
        fprintf(stderr, "[Erreur] Montage ISO echoue.\nSortie : %s\n", output);
        return -1;
    }

    // Parser la lettre de lecteur dans la sortie
    for (int i = 0; output[i]; i++) {
        if (isalpha((unsigned char)output[i])) {
            iso_drive = (char)toupper((unsigned char)output[i]);
            break;
        }
    }

    if (!iso_drive) {
        fprintf(stderr, "[Erreur] Lettre de lecteur ISO introuvable.\nSortie : %s\n",
                output);
        return -1;
    }
    printf("[Pleco] ISO monte sur %c:\n", iso_drive);
    if (progress_cb) progress_cb(10, 100);

    // ── Étape 2 : Copier les fichiers avec robocopy ───────────────────────
    // /E   : tous les sous-dossiers y compris vides
    // /COPYALL : copier tous les attributs
    // /R:1 /W:1 : 1 tentative, 1 seconde d'attente
    // /NFL /NDL /NJH /NJS : pas de log verbeux
    printf("[Pleco] Copie des fichiers vers %c:...\n", drive_letter);

    char copy_cmd[256];
    snprintf(copy_cmd, sizeof(copy_cmd),
        "robocopy %c:\\ %c:\\ /E /COPYALL /R:1 /W:1 /NFL /NDL /NJH /NJS",
        iso_drive, drive_letter);

    // Note : run_process_with_input gère déjà le code retour 1 de robocopy
    run_process_with_input(copy_cmd, NULL, output, sizeof(output));
    if (progress_cb) progress_cb(85, 100);

    // ── Étape 3 : Vérifier la présence de bootx64.efi ────────────────────
    char efi_dir[16];
    snprintf(efi_dir, sizeof(efi_dir), "%c:\\EFI", drive_letter);

    char efi_rel[MAX_PATH] = {0};

    if (!find_efi_binary(efi_dir, "\\EFI", efi_rel, sizeof(efi_rel))) {
        // EFI absent — peut-être que robocopy a sauté des gros fichiers avant EFI
        // On recopie uniquement le dossier EFI depuis l'ISO encore monté
        printf("[Pleco] EFI absent — copie ciblee du dossier EFI...\n");

        char src_efi[32], dst_efi[32];
        snprintf(src_efi, sizeof(src_efi), "%c:\\EFI", iso_drive);
        snprintf(dst_efi, sizeof(dst_efi), "%c:\\EFI", drive_letter);
        snprintf(copy_cmd, sizeof(copy_cmd),
            "robocopy %s %s /E /COPYALL /R:1 /W:1 /NFL /NDL /NJH /NJS",
            src_efi, dst_efi);
        run_process_with_input(copy_cmd, NULL, output, sizeof(output));

        if (!find_efi_binary(efi_dir, "\\EFI", efi_rel, sizeof(efi_rel))) {
            fprintf(stderr,
                "[Erreur] bootx64.efi introuvable sous %c:\\EFI\\\n"
                "         L'ISO n'est peut-etre pas un ISO Linux UEFI.\n",
                drive_letter);
            // Démonter avant de quitter
            snprintf(ps_cmd, sizeof(ps_cmd),
                "powershell -NoProfile -NonInteractive -Command "
                "\"Dismount-DiskImage -ImagePath '%s'\"",
                iso_ps_path);
            run_process_with_input(ps_cmd, NULL, output, sizeof(output));
            return -1;
        }
    }

    printf("[Pleco] Binaire EFI trouve : %c:%s\n", drive_letter, efi_rel);

    // Remplir out_efi_path si fourni
    if (out_efi_path && efi_path_size > 0) {
        strncpy(out_efi_path, efi_rel, efi_path_size - 1);
        out_efi_path[efi_path_size - 1] = '\0';
    }

    // ── Étape 4 : Démonter l'ISO ──────────────────────────────────────────
    snprintf(ps_cmd, sizeof(ps_cmd),
        "powershell -NoProfile -NonInteractive -Command "
        "\"Dismount-DiskImage -ImagePath '%s'\"",
        iso_ps_path);
    run_process_with_input(ps_cmd, NULL, output, sizeof(output));
    printf("[Pleco] ISO demonte.\n");

    if (progress_cb) progress_cb(100, 100);
    return 0;
}
