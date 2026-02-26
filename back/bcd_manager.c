// bcd_manager.c
#include "header/bcd_manager.h"
#include "header/utils.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

static int run_bcdedit(const char* args, char* output, DWORD output_size) {
    char command[1024];
    snprintf(command, sizeof(command), "bcdedit %s", args);
    return run_process_with_input(command, NULL, output, output_size);
}


int bcd_backup(const char* backup_path) {
    char args[512];
    char output[1024];
    snprintf(args, sizeof(args), "/export \"%s\"", backup_path);

    if (run_bcdedit(args, output, sizeof(output)) != 0) {
        fprintf(stderr, "[Erreur] Impossible de sauvegarder le BCD.\n");
        return -1;
    }
    printf("[Pleco] BCD sauvegardé dans : %s\n", backup_path);
    return 0;
}

int bcd_restore(const char* backup_path) {
    char args[512];
    char output[1024];
    snprintf(args, sizeof(args), "/import \"%s\"", backup_path);
    return run_bcdedit(args, output, sizeof(output));
}

// ── Créer une entrée BCD ──────────────────────────────────────────────────

int bcd_create_entry(const char* description, char* out_identifier) {
    char args[512];
    char output[4096];

    snprintf(args, sizeof(args),
             "/create /d \"%s\" /application BOOTSECTOR", description);

    if (run_bcdedit(args, output, sizeof(output)) != 0) {
        fprintf(stderr, "[Erreur] bcdedit /create a échoué.\n");
        return -1;
    }

    // Parser l'identifiant dans la sortie
    // La sortie ressemble à :
    // "L'entrée a bien été créée dans {xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}."
    char* start = strchr(output, '{');
    char* end   = strchr(output, '}');

    if (!start || !end || end <= start) {
        fprintf(stderr, "[Erreur] Impossible de parser l'identifiant BCD.\n");
        fprintf(stderr, "Sortie bcdedit : %s\n", output);
        return -1;
    }

    // Copier l'identifiant (avec les accolades)
    int len = (int)(end - start) + 1;
    if (len >= BCD_ID_MAX) len = BCD_ID_MAX - 1;
    strncpy(out_identifier, start, len);
    out_identifier[len] = '\0';

    printf("[Pleco] Entrée BCD créée : %s\n", out_identifier);
    return 0;
}

// ── Configurer l'entrée ───────────────────────────────────────────────────

int bcd_configure_entry(const char* id, char drive_letter) {
    char args[512];
    char output[1024];
    int  result = 0;

    // 1. Pointer vers la partition temporaire
    snprintf(args, sizeof(args), "/set %s device partition=%c:", id, drive_letter);
    result += run_bcdedit(args, output, sizeof(output));

    // 2. Chemin vers le secteur de boot GRUB sur la partition
    snprintf(args, sizeof(args), "/set %s path \\boot\\grub\\i386-pc\\core.img", id);
    result += run_bcdedit(args, output, sizeof(output));

    // 3. Mettre en premier dans l'ordre de démarrage
    snprintf(args, sizeof(args), "/displayorder %s /addfirst", id);
    result += run_bcdedit(args, output, sizeof(output));

    // 4. Définir comme entrée par défaut
    snprintf(args, sizeof(args), "/default %s", id);
    result += run_bcdedit(args, output, sizeof(output));

    // 5. Timeout de 10 secondes
    run_bcdedit("/timeout 10", output, sizeof(output));

    if (result != 0) {
        fprintf(stderr, "[Erreur] Configuration BCD incomplète.\n");
        return -1;
    }

    printf("[Pleco] BCD configuré pour booter sur %c:\n", drive_letter);
    return 0;
}

// ── Supprimer l'entrée ────────────────────────────────────────────────────

int bcd_delete_entry(const char* id) {
    char args[256];
    char output[1024];
    snprintf(args, sizeof(args), "/delete %s /cleanup", id);
    return run_bcdedit(args, output, sizeof(output));
}
