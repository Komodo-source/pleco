#ifndef ISO_WRITER_H
#define ISO_WRITER_H

typedef void (*progress_callback_t)(unsigned long long written,
                                     unsigned long long total);


int verify_iso_sha256(const char* iso_path, const char* expected_hash);

int write_iso_to_partition(
    const char* iso_path,
    char drive_letter,
    progress_callback_t progress_cb,
    char* out_efi_path,
    int   efi_path_size
);

#endif
