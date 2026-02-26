// bcd_manager.c
#include "header/bcd_manager.h"
#include "header/utils.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

// Lance bcdedit avec des arguments et capture la sortie
static int run_bcdedit(const char* args, char* output, DWORD output_size) {
    char command[1024];
    snprintf(command, sizeof(command), "bcdedit %s", args);
    return run_process_with_input(command, NULL, output, output_size);
}

// ── Backup / Restore ──────────────────────────────────────────────────────

int bcd_backup(const char* backup_path) {
    char args[512];
    char output[1024];
    snprintf(args, sizeof(args), "/export \"%s\"", backup_path);

    if (run_bcdedit(args, output, sizeof(output)) != 0) {
        fprintf(stderr, "[Erreur] Impossible de sauvegarder le BCD.\n");
        return -1;
    }
    printf("[Pleco] BCD sauvegarde dans : %s\n", backup_path);
    return 0;
}

int bcd_restore(const char* backup_path) {
    char args[512];
    char output[1024];

    // Vérifier que le fichier de backup existe
    if (GetFileAttributesA(backup_path) == INVALID_FILE_ATTRIBUTES) {
        fprintf(stderr, "[Erreur] Fichier de backup BCD introuvable : %s\n", backup_path);
        return -1;
    }

    snprintf(args, sizeof(args), "/import \"%s\"", backup_path);
    if (run_bcdedit(args, output, sizeof(output)) != 0) {
        fprintf(stderr, "[Erreur] Restauration BCD echouee.\n");
        fprintf(stderr, "         Commande manuelle : bcdedit /import \"%s\"\n", backup_path);
        return -1;
    }

    printf("[Pleco] BCD restaure avec succes.\n");
    return 0;
}

// ── Créer une entrée BCD ──────────────────────────────────────────────────

int bcd_create_entry(const char* description, char* out_identifier) {
    char args[512];
    char output[4096];

    // Initialiser out_identifier à vide pour détecter les échecs partiels
    out_identifier[0] = '\0';

    snprintf(args, sizeof(args),
             "/create /d \"%s\" /application BOOTAPP", description);

    if (run_bcdedit(args, output, sizeof(output)) != 0) {
        fprintf(stderr, "[Erreur] bcdedit /create a echoue.\n");
        return -1;
    }

    // Parser l'identifiant dans la sortie
    // Format attendu : "...{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}..."
    char* start = strchr(output, '{');
    char* end   = strchr(output, '}');

    if (!start || !end || end <= start) {
        fprintf(stderr, "[Erreur] Impossible de parser l'identifiant BCD.\n");
        fprintf(stderr, "Sortie bcdedit : %s\n", output);
        return -1;
    }

    int len = (int)(end - start) + 1;
    if (len >= BCD_ID_MAX) len = BCD_ID_MAX - 1;
    strncpy(out_identifier, start, len);
    out_identifier[len] = '\0';

    printf("[Pleco] Entree BCD creee : %s\n", out_identifier);
    return 0;
}

// ── Configurer l'entrée ───────────────────────────────────────────────────

int bcd_configure_entry(const char* id, char drive_letter, const char* efi_path) {
    char args[512];
    char output[1024];
    int  failed = 0;

    // 1. Pointer vers la partition
    snprintf(args, sizeof(args), "/set %s device partition=%c:", id, drive_letter);
    if (run_bcdedit(args, output, sizeof(output)) != 0) {
        fprintf(stderr, "[Erreur] bcdedit /set device echoue.\n");
        failed = 1;
    }

    // 2. Chemin vers le binaire EFI (détecté dynamiquement par iso_writer)
    if (!failed) {
        snprintf(args, sizeof(args),
                 "/set %s path %s", id, efi_path);
        if (run_bcdedit(args, output, sizeof(output)) != 0) {
            fprintf(stderr, "[Erreur] bcdedit /set path echoue.\n");
            failed = 1;
        }
    }

    // 3. Premier dans l'ordre de démarrage
    if (!failed) {
        snprintf(args, sizeof(args), "/displayorder %s /addfirst", id);
        if (run_bcdedit(args, output, sizeof(output)) != 0) {
            fprintf(stderr, "[Erreur] bcdedit /displayorder echoue.\n");
            failed = 1;
        }
    }

    // 4. Entrée par défaut
    if (!failed) {
        snprintf(args, sizeof(args), "/default %s", id);
        if (run_bcdedit(args, output, sizeof(output)) != 0) {
            fprintf(stderr, "[Erreur] bcdedit /default echoue.\n");
            failed = 1;
        }
    }

    // 5. Timeout
    if (!failed) {
        run_bcdedit("/timeout 10", output, sizeof(output));
    }

    if (failed) {
        // Si la configuration a échoué à mi-chemin, supprimer l'entrée
        // pour ne pas laisser une entrée BCD incomplète qui casserait le boot
        fprintf(stderr, "[Pleco] Configuration incomplete — suppression de l'entree BCD...\n");
        bcd_delete_entry(id);
        return -1;
    }

    printf("[Pleco] BCD configure pour booter sur %c:\n", drive_letter);
    return 0;
}

// ── Supprimer l'entrée ────────────────────────────────────────────────────

int bcd_delete_entry(const char* id) {
    char args[256];
    char output[1024];

    if (!id || strlen(id) == 0) return 0;  // Rien à supprimer

    snprintf(args, sizeof(args), "/delete %s /cleanup", id);
    if (run_bcdedit(args, output, sizeof(output)) != 0) {
        fprintf(stderr, "[Attention] Impossible de supprimer l'entree BCD %s.\n", id);
        fprintf(stderr, "            Commande manuelle : bcdedit /delete %s /cleanup\n", id);
        return -1;
    }

    printf("[Pleco] Entree BCD %s supprimee.\n", id);
    return 0;
}
