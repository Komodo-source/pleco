#ifndef ISO_WRITER_H
#define ISO_WRITER_H

// Callback de progression : (valeur_actuelle, valeur_max)
typedef void (*progress_callback_t)(unsigned long long written,
                                     unsigned long long total);

// Vérifie le hash SHA-256 de l'ISO
// Retourne 1 si OK, 0 si invalide
int verify_iso_sha256(const char* iso_path, const char* expected_hash);

// Monte l'ISO via PowerShell, copie les fichiers sur drive_letter: avec robocopy,
// détecte le binaire EFI (bootx64.efi) et remplit out_efi_path.
// Retourne 0 en succès, -1 en erreur.
int write_iso_to_partition(
    const char* iso_path,
    char        drive_letter,
    progress_callback_t progress_cb,
    char*       out_efi_path,
    int         efi_path_size
);

#endif
