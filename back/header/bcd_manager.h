// bcd_manager.h
#ifndef BCD_MANAGER_H
#define BCD_MANAGER_H

// Taille maximale d'un identifiant BCD
// Format : {xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}
#define BCD_ID_MAX 64

// Sauvegarder le BCD dans un fichier
int bcd_backup(const char* backup_path);

// Restaurer le BCD depuis un fichier (en cas d'erreur)
int bcd_restore(const char* backup_path);

// Créer une nouvelle entrée de boot
// Remplit `out_identifier` avec l'ID généré (ex: "{abc123...}")
int bcd_create_entry(const char* description, char* out_identifier);

// Configurer l'entrée pour pointer vers notre partition
int bcd_configure_entry(const char* identifier, char drive_letter);

// Supprimer l'entrée (nettoyage post-installation)
int bcd_delete_entry(const char* identifier);

#endif
