// main.c
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "header/partitioning.h"
#include "header/iso_writer.h"
#include "header/bcd_manager.h"

#define TEMP_DRIVE_LETTER   'P'
#define PARTITION_PATH      "\\\\.\\P:"
#define BCD_BACKUP_PATH     "C:\\Windows\\Temp\\pleco_bcd_backup.bcd"
#define ISO_SIZE_EXTRA_MB   512


void on_progress(unsigned long long written, unsigned long long total) {
    int percent = (int)((written * 100ULL) / total);
    // Barre de progression console
    printf("\r  [");
    for (int i = 0; i < 40; i++) {
        printf(i < (percent * 40 / 100) ? "#" : "-");
    }
    printf("] %d%%   ", percent);
    fflush(stdout);
}

// ── Vérifier les droits admin ─────────────────────────────────────────────

int is_admin(void) {
    BOOL is_admin = FALSE;
    HANDLE token;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION elevation;
        DWORD size;
        if (GetTokenInformation(token, TokenElevation, &elevation,
                                sizeof(elevation), &size)) {
            is_admin = elevation.TokenIsElevated;
        }
        CloseHandle(token);
    }
    return is_admin;
}

// ── Redémarrage ───────────────────────────────────────────────────────────

void reboot_in_seconds(int seconds) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd),
             "shutdown /r /t %d /c \"Pleco va démarrer l'installateur Linux\"",
             seconds);
    system(cmd);
}

// ── Point d'entrée ────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {

    printf("=== Pleco — Linux Installer ===\n\n");

    // 1. Vérifier les droits administrateur
    if (!is_admin()) {
        fprintf(stderr,
            "[Erreur] Ce programme doit être exécuté en tant qu'Administrateur.\n"
            "Clic droit → Exécuter en tant qu'administrateur.\n");
        return 1;
    }
    printf("[OK] Droits administrateur confirmés.\n");

    // Arguments attendus :
    // pleco.exe <chemin_iso> <hash_sha256> <mode>
    // ex: pleco.exe C:\ubuntu.iso abc123... dualboot
    if (argc < 4) {
        fprintf(stderr, "Usage: pleco.exe <iso_path> <sha256_hash> <dualboot|replace>\n");
        return 1;
    }

    const char* iso_path    = argv[1];
    const char* iso_hash    = argv[2];
    const char* install_mode = argv[3];

    printf("[Info] ISO       : %s\n", iso_path);
    printf("[Info] Mode      : %s\n\n", install_mode);

    // 2. Vérifier l'espace disponible
    unsigned long long free_mb = get_free_space_mb();
    printf("[Info] Espace libre sur C: : %llu Mo\n", free_mb);
    if (free_mb < 9000) {
        fprintf(stderr, "[Erreur] Espace insuffisant (minimum 9 Go requis).\n");
        return 1;
    }

    // 3. Vérifier le hash SHA-256 de l'ISO
    printf("\n[Étape 1/5] Vérification de l'ISO...\n");
    if (!verify_iso_sha256(iso_path, iso_hash)) {
        fprintf(stderr, "[Erreur] L'ISO est corrompu ou modifié. Abandon.\n");
        return 1;
    }

    // 4. Calculer la taille de la partition nécessaire
    HANDLE hISO = CreateFileA(iso_path, GENERIC_READ, FILE_SHARE_READ,
                              NULL, OPEN_EXISTING, 0, NULL);
    LARGE_INTEGER iso_size = {0};
    GetFileSizeEx(hISO, &iso_size);
    CloseHandle(hISO);
    unsigned int partition_size_mb =
        (unsigned int)(iso_size.QuadPart / (1024 * 1024)) + ISO_SIZE_EXTRA_MB;

    // 5. Sauvegarder le BCD
    printf("\n[Étape 2/5] Sauvegarde de la configuration de boot...\n");
    if (bcd_backup(BCD_BACKUP_PATH) != 0) {
        fprintf(stderr, "[Erreur] Impossible de sauvegarder le BCD. Abandon.\n");
        return 1;
    }

    // 6. Créer la partition temporaire
    printf("\n[Étape 3/5] Création de la partition (%u Mo)...\n", partition_size_mb);
    if (create_temp_partition(partition_size_mb, TEMP_DRIVE_LETTER) != 0) {
        fprintf(stderr, "[Erreur] Partitionnement échoué.\n");
        bcd_restore(BCD_BACKUP_PATH);
        return 1;
    }

    // Attendre que Windows monte le volume
    Sleep(2000);

    // 7. Écrire l'ISO sur la partition
    printf("\n[Étape 4/5] Écriture de l'ISO sur %c:...\n", TEMP_DRIVE_LETTER);
    if (write_iso_to_partition(iso_path, PARTITION_PATH, on_progress) != 0) {
        fprintf(stderr, "\n[Erreur] Écriture de l'ISO échouée.\n");
        delete_partition(TEMP_DRIVE_LETTER);
        bcd_restore(BCD_BACKUP_PATH);
        return 1;
    }
    printf("\n");

    // 8. Configurer le BCD
    printf("\n[Étape 5/5] Configuration du démarrage...\n");
    char bcd_id[BCD_ID_MAX] = {0};

    if (bcd_create_entry("Pleco Linux Installer", bcd_id) != 0) {
        fprintf(stderr, "[Erreur] Création de l'entrée BCD échouée.\n");
        delete_partition(TEMP_DRIVE_LETTER);
        bcd_restore(BCD_BACKUP_PATH);
        return 1;
    }

    if (bcd_configure_entry(bcd_id, TEMP_DRIVE_LETTER) != 0) {
        fprintf(stderr, "[Erreur] Configuration de l'entrée BCD échouée.\n");
        bcd_delete_entry(bcd_id);
        delete_partition(TEMP_DRIVE_LETTER);
        bcd_restore(BCD_BACKUP_PATH);
        return 1;
    }

    // 9. Tout est prêt
    printf("\n╔══════════════════════════════════════╗\n");
    printf("║   Installation prête !               ║\n");
    printf("║   Le PC va redémarrer dans 10 sec.   ║\n");
    printf("║   Linux s'installera au démarrage.   ║\n");
    printf("╚══════════════════════════════════════╝\n\n");

    reboot_in_seconds(10);
    return 0;
}
