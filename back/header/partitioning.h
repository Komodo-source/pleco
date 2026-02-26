// partitioning.h
#ifndef PARTITIONING_H
#define PARTITIONING_H

int create_temp_partition(unsigned int size_mb, char drive_letter);

int delete_partition(char drive_letter);

unsigned long long get_free_space_mb(void);

#endif
