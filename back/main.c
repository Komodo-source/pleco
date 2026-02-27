// main.c
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "header/partitioning.h"
#include "header/iso_writer.h"
#include "header/bcd_manager.h"

#define TEMP_DRIVE_LETTER  'P'
#define BCD_BACKUP_PATH    "C:\\Windows\\Temp\\pleco_bcd_backup.bcd"
#define ISO_SIZE_EXTRA_MB  512

// ── Callback de progression ───────────────────────────────────────────────
// Signature : (unsigned long long, unsigned long long) pour correspondre
// au typedef dans iso_writer.h

void on_progress(unsigned long long written, unsigned long long total) {
    int percent = (total > 0) ? (int)((written * 100ULL) / total) : 0;
    printf("\r  [");
    for (int i = 0; i < 40; i++) printf(i < (percent * 40 / 100) ? "#" : "-");
    printf("] %d%%   ", percent);
    fflush(stdout);
}

// ── Vérifier les droits admin ─────────────────────────────────────────────

int is_admin(void) {
    BOOL result = FALSE;
    HANDLE token;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION elevation;
        DWORD size;
        if (GetTokenInformation(token, TokenElevation, &elevation,
                                sizeof(elevation), &size)) {
            result = elevation.TokenIsElevated;
        }
        CloseHandle(token);
    }
    return result;
}

// ── Redémarrage ───────────────────────────────────────────────────────────

void reboot_in_seconds(int seconds) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd),
        "shutdown /r /t %d /c \"Pleco va demarrer l'installateur Linux\"",
        seconds);
    system(cmd);
}

// ── Nettoyage d'urgence ───────────────────────────────────────────────────

void emergency_cleanup(const char* bcd_id) {
    fprintf(stderr, "\n[Pleco] Nettoyage en cours...\n");

    if (bcd_id && strlen(bcd_id) > 0) {
        fprintf(stderr, "[Pleco] Suppression entree BCD %s...\n", bcd_id);
        bcd_delete_entry(bcd_id);
    }

    fprintf(stderr, "[Pleco] Restauration BCD original...\n");
    if (bcd_restore(BCD_BACKUP_PATH) == 0) {
        fprintf(stderr, "[Pleco] BCD restaure.\n");
    } else {
        fprintf(stderr,
            "[ATTENTION] Restauration automatique echouee.\n"
            "            Commande manuelle : bcdedit /import \"%s\"\n",
            BCD_BACKUP_PATH);
    }

    fprintf(stderr, "[Pleco] Suppression partition temporaire...\n");
    delete_partition(TEMP_DRIVE_LETTER);

    fprintf(stderr, "[Pleco] Nettoyage termine.\n\n");
}

// ── Point d'entrée ────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {

    printf("=== Pleco - Linux Installer ===\n\n");

    if (!is_admin()) {
        fprintf(stderr,
            "[Erreur] Droits administrateur requis.\n"
            "         Clic droit -> Executer en tant qu'administrateur.\n");
        return 1;
    }
    printf("[OK] Droits administrateur confirmes.\n");

    if (argc < 4) {
        fprintf(stderr,
            "Usage: pleco.exe <iso_path> <sha256_hash> <dualboot|replace>\n"
            "Ex:    pleco.exe ubuntu.iso abc123... dualboot\n");
        return 1;
    }

    const char* iso_path     = argv[1];
    const char* iso_hash     = argv[2];
    const char* install_mode = argv[3];

    char bcd_id[BCD_ID_MAX]  = {0};
    char efi_path[MAX_PATH]  = {0};

    printf("[Info] ISO  : %s\n", iso_path);
    printf("[Info] Mode : %s\n\n", install_mode);

    // ── Étape 0 : Espace disponible ───────────────────────────────────────

    unsigned long long free_mb = get_free_space_mb();
    printf("[Info] Espace libre : %llu Mo\n", free_mb);
    if (free_mb < 9000) {
        fprintf(stderr,
            "[Erreur] Espace insuffisant (%llu Mo, 9000 Mo requis).\n"
            "         diskpart > select disk 0 > select partition 4\n"
            "                  > shrink desired=15000 minimum=9000\n",
            free_mb);
        return 1;
    }

    // ── Étape 1 : Vérifier le hash ────────────────────────────────────────

    printf("\n[Etape 1/5] Verification de l'ISO...\n");
    if (!verify_iso_sha256(iso_path, iso_hash)) {
        fprintf(stderr, "[Erreur] Hash incorrect ou ISO corrompu.\n");
        return 1;
    }

    // Taille de partition nécessaire = taille ISO + marge
    HANDLE hISO = CreateFileA(iso_path, GENERIC_READ, FILE_SHARE_READ,
                              NULL, OPEN_EXISTING, 0, NULL);
    if (hISO == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "[Erreur] Impossible d'ouvrir l'ISO.\n");
        return 1;
    }
    LARGE_INTEGER iso_size;
    iso_size.QuadPart = 0;
    GetFileSizeEx(hISO, &iso_size);
    CloseHandle(hISO);

    unsigned int partition_size_mb =
        (unsigned int)(iso_size.QuadPart / (1024 * 1024)) + ISO_SIZE_EXTRA_MB;

    // ── Étape 2 : Sauvegarder le BCD ─────────────────────────────────────

    printf("\n[Etape 2/5] Sauvegarde BCD...\n");
    if (bcd_backup(BCD_BACKUP_PATH) != 0) {
        fprintf(stderr, "[Erreur] Sauvegarde BCD echouee. Abandon.\n");
        return 1;
    }

    // ── Étape 3 : Créer la partition ──────────────────────────────────────

    printf("\n[Etape 3/5] Creation de la partition (%u Mo)...\n", partition_size_mb);
    if (create_temp_partition(partition_size_mb, TEMP_DRIVE_LETTER) != 0) {
        fprintf(stderr, "[Erreur] Creation partition echouee.\n");
        emergency_cleanup(NULL);
        return 1;
    }

    Sleep(3000);

    // ── Étape 4 : Copier les fichiers ISO ─────────────────────────────────

    printf("\n[Etape 4/5] Copie de l'ISO vers %c:...\n", TEMP_DRIVE_LETTER);
    if (write_iso_to_partition(iso_path, TEMP_DRIVE_LETTER, on_progress,
                               efi_path, sizeof(efi_path)) != 0) {
        fprintf(stderr, "[Erreur] Copie ISO echouee.\n");
        emergency_cleanup(NULL);
        return 1;
    }
    printf("\n");

    // Vérifier que le chemin EFI a bien été détecté
    if (strlen(efi_path) == 0) {
        fprintf(stderr,
            "[Erreur] Chemin EFI non detecte. ISO non UEFI ?\n");
        emergency_cleanup(NULL);
        return 1;
    }
    printf("[Pleco] Chemin EFI : %s\n", efi_path);

    // ── Étape 5 : Configurer le BCD ───────────────────────────────────────
    // Uniquement si la copie ISO a réussi

    printf("\n[Etape 5/5] Configuration du demarrage...\n");

    if (bcd_create_entry("Pleco Linux Installer", bcd_id) != 0) {
        fprintf(stderr, "[Erreur] Creation entree BCD echouee.\n");
        emergency_cleanup(NULL);
        return 1;
    }

    if (bcd_configure_entry(bcd_id, TEMP_DRIVE_LETTER, efi_path) != 0) {
        fprintf(stderr, "[Erreur] Configuration BCD echouee.\n");
        emergency_cleanup(bcd_id);
        return 1;
    }

    // ── Succès ────────────────────────────────────────────────────────────

    printf("\n");
    printf("╔══════════════════════════════════════╗\n");
    printf("║   Installation prete !               ║\n");
    printf("║   Redemarrage dans 10 secondes...    ║\n");
    printf("╚══════════════════════════════════════╝\n\n");

    reboot_in_seconds(10);
    return 0;
}
