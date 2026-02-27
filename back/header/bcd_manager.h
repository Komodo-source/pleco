#ifndef BCD_MANAGER_H
#define BCD_MANAGER_H

#define BCD_ID_MAX 64

int bcd_backup(const char* backup_path);
int bcd_restore(const char* backup_path);
int bcd_create_entry(const char* description, char* out_identifier);

// efi_path : chemin relatif vers le binaire EFI, ex: \EFI\BOOT\BOOTx64.EFI
int bcd_configure_entry(const char* id, char drive_letter, const char* efi_path);
int bcd_delete_entry(const char* id);

#endif
