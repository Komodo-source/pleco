// main.c
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "header/partitioning.h"
#include "header/iso_writer.h"
#include "header/bcd_manager.h"

#define TEMP_DRIVE_LETTER   'P'
#define BCD_BACKUP_PATH     "C:\\Windows\\Temp\\pleco_bcd_backup.bcd"
#define ISO_SIZE_EXTRA_MB   512

// ── Callback de progression ───────────────────────────────────────────────

void on_progress(unsigned long long written, unsigned long long total) {
    int percent = (total > 0) ? (int)((written * 100ULL) / total) : 0;
    printf("\r  [");
    for (int i = 0; i < 40; i++) {
        printf(i < (percent * 40 / 100) ? "#" : "-");
    }
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
// Appelé à chaque erreur pour remettre le système dans son état initial

void emergency_cleanup(const char* bcd_id) {
    fprintf(stderr, "\n[Pleco] Nettoyage en cours...\n");

    // 1. Supprimer l'entrée BCD si elle a été créée
    if (bcd_id && strlen(bcd_id) > 0) {
        fprintf(stderr, "[Pleco] Suppression de l'entree BCD %s...\n", bcd_id);
        bcd_delete_entry(bcd_id);
    }

    // 2. Restaurer le BCD original
    fprintf(stderr, "[Pleco] Restauration du BCD original...\n");
    if (bcd_restore(BCD_BACKUP_PATH) == 0) {
        fprintf(stderr, "[Pleco] BCD restaure avec succes.\n");
    } else {
        fprintf(stderr, "[ATTENTION] Impossible de restaurer le BCD automatiquement.\n");
        fprintf(stderr, "            Fichier de backup : %s\n", BCD_BACKUP_PATH);
        fprintf(stderr, "            Commande manuelle : bcdedit /import \"%s\"\n",
                BCD_BACKUP_PATH);
    }

    // 3. Supprimer la partition temporaire
    fprintf(stderr, "[Pleco] Suppression de la partition temporaire...\n");
    delete_partition(TEMP_DRIVE_LETTER);

    fprintf(stderr, "[Pleco] Nettoyage termine. Aucune modification permanente.\n\n");
}

// ── Point d'entrée ────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {

    printf("=== Pleco - Linux Installer ===\n\n");

    // Vérifier les droits administrateur
    if (!is_admin()) {
        fprintf(stderr,
            "[Erreur] Ce programme doit etre execute en tant qu'Administrateur.\n"
            "Clic droit -> Executer en tant qu'administrateur.\n");
        return 1;
    }
    printf("[OK] Droits administrateur confirmes.\n");

    if (argc < 4) {
        fprintf(stderr,
            "Usage: pleco.exe <iso_path> <sha256_hash> <dualboot|replace>\n"
            "Exemple: pleco.exe ubuntu.iso abc123... dualboot\n");
        return 1;
    }

    const char* iso_path     = argv[1];
    const char* iso_hash     = argv[2];
    const char* install_mode = argv[3];

    // Identifiant BCD — initialisé vide pour le nettoyage d'urgence
    char bcd_id[BCD_ID_MAX] = {0};

    printf("[Info] ISO  : %s\n", iso_path);
    printf("[Info] Mode : %s\n\n", install_mode);

    // ── Étape 0 : Vérifier l'espace disponible ────────────────────────────

    unsigned long long free_mb = get_free_space_mb();
    printf("[Info] Espace libre sur C: : %llu Mo\n", free_mb);
    if (free_mb < 9000) {
        fprintf(stderr,
            "[Erreur] Espace insuffisant (%llu Mo disponibles, 9000 Mo requis).\n"
            "         Utilisez diskpart pour reduire C: :\n"
            "         > select disk 0\n"
            "         > select partition 4\n"
            "         > shrink desired=15000 minimum=9000\n",
            free_mb);
        return 1;
    }

    // ── Étape 1 : Vérifier le hash SHA-256 ───────────────────────────────

    printf("\n[Etape 1/5] Verification de l'ISO...\n");
    if (!verify_iso_sha256(iso_path, iso_hash)) {
        fprintf(stderr,
            "[Erreur] L'ISO est corrompu ou le hash est incorrect.\n"
            "         Telechargez le hash correct sur le site officiel.\n");
        return 1;
    }

    // Calculer la taille de partition nécessaire
    HANDLE hISO = CreateFileA(iso_path, GENERIC_READ, FILE_SHARE_READ,
                              NULL, OPEN_EXISTING, 0, NULL);
    if (hISO == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "[Erreur] Impossible d'ouvrir l'ISO.\n");
        return 1;
    }
    LARGE_INTEGER iso_size = {0};
    GetFileSizeEx(hISO, &iso_size);
    CloseHandle(hISO);

    unsigned int partition_size_mb =
        (unsigned int)(iso_size.QuadPart / (1024 * 1024)) + ISO_SIZE_EXTRA_MB;

    // ── Étape 2 : Sauvegarder le BCD ─────────────────────────────────────

    printf("\n[Etape 2/5] Sauvegarde de la configuration de boot...\n");
    if (bcd_backup(BCD_BACKUP_PATH) != 0) {
        fprintf(stderr, "[Erreur] Impossible de sauvegarder le BCD. Abandon.\n");
        return 1;
        // Pas de cleanup ici — rien n'a encore été modifié
    }

    // ── Étape 3 : Créer la partition temporaire ───────────────────────────

    printf("\n[Etape 3/5] Creation de la partition (%u Mo)...\n", partition_size_mb);
    if (create_temp_partition(partition_size_mb, TEMP_DRIVE_LETTER) != 0) {
        fprintf(stderr, "[Erreur] Partitionnement echoue.\n");
        // Pas de BCD créé encore, pas besoin de passer bcd_id
        emergency_cleanup(NULL);
        return 1;
    }

    // Attendre que Windows monte le volume
    Sleep(3000);

    // ── Étape 4 : Écrire l'ISO ────────────────────────────────────────────

    printf("\n[Etape 4/5] Ecriture de l'ISO sur %c:...\n", TEMP_DRIVE_LETTER);
    if (write_iso_to_partition(iso_path, TEMP_DRIVE_LETTER, on_progress) != 0) {
        fprintf(stderr, "\n[Erreur] Ecriture de l'ISO echouee.\n");
        // BCD pas encore modifié — cleanup sans bcd_id
        emergency_cleanup(NULL);
        return 1;
    }
    printf("\n");

    // ── Étape 5 : Configurer le BCD ───────────────────────────────────────
    // SEULEMENT si l'ISO a été écrit avec succès

    printf("\n[Etape 5/5] Configuration du demarrage...\n");

    if (bcd_create_entry("Pleco Linux Installer", bcd_id) != 0) {
        fprintf(stderr, "[Erreur] Creation de l'entree BCD echouee.\n");
        emergency_cleanup(NULL);  // bcd_id est vide, pas de suppression BCD
        return 1;
    }

    if (bcd_configure_entry(bcd_id, TEMP_DRIVE_LETTER) != 0) {
        fprintf(stderr, "[Erreur] Configuration BCD echouee.\n");
        // Ici bcd_id est rempli — on le supprime dans le cleanup
        emergency_cleanup(bcd_id);
        return 1;
    }

    // ── Tout est bon ──────────────────────────────────────────────────────

    printf("\n");
    printf("╔══════════════════════════════════════╗\n");
    printf("║   Installation prete !               ║\n");
    printf("║   Redemarrage dans 10 secondes...    ║\n");
    printf("║   Linux s'installera au demarrage.   ║\n");
    printf("╚══════════════════════════════════════╝\n\n");

    reboot_in_seconds(10);
    return 0;
}
