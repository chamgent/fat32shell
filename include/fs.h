#ifndef FS_H
#define FS_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "fat32.h"

#define MAX_OPEN_FILES 10
#define MAX_NAME_LEN   11

typedef struct {
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sector_count;
    uint8_t  num_FATs;
    uint32_t FATSz32;
    uint32_t root_cluster;
    uint32_t tot_sec;
    uint32_t total_clusters;
    uint32_t first_FAT_sector;
    uint32_t first_data_sector;
    uint64_t image_size_bytes;
    uint32_t cwd_cluster;
    char image_name[256];

    FILE *fp;
} FSInfo;

typedef struct {
    bool in_use;
    char name[MAX_NAME_LEN+1];
    uint32_t cluster;
    uint32_t size;
    uint8_t attr;
    char mode[4]; 
    uint32_t offset;
    uint32_t dir_entry_sector;
    uint32_t dir_entry_offset;
    char path[512]; 
} OpenFileEntry;

extern FSInfo fsinfo;
extern OpenFileEntry open_files[MAX_OPEN_FILES];


extern char current_path[512]; 

int fs_mount(const char *image_path);
void fs_unmount();
int fs_info();
int fs_cd(const char *dirname);
int fs_ls();
int fs_mkdir(const char *dirname);
int fs_creat(const char *filename);
int fs_open(const char *filename, const char *flags);
int fs_close(const char *filename);
int fs_lsof();
int fs_size(const char *filename);
int fs_lseek(const char *filename, uint32_t offset);
int fs_read(const char *filename, uint32_t size);
int fs_write(const char *filename, const char *str);
int fs_rename(const char *oldname, const char *newname);
int fs_rm(const char *filename);
int fs_rmdir(const char *dirname);

int fs_find_entry_in_dir(uint32_t dir_cluster, const char *name, DirEntry *out_entry, uint32_t *out_sector, uint32_t *out_offset);
bool fs_name_exists_in_dir(uint32_t dir_cluster, const char *name);
int fs_allocate_cluster_chain(uint32_t count, uint32_t *start_cluster);
int fs_free_cluster_chain(uint32_t start_cluster);
int fs_update_dir_entry(uint32_t sector, uint32_t offset, DirEntry *entry);
int fs_read_cluster_chain(uint32_t start_cluster, uint8_t *buffer, uint32_t offset, uint32_t size);
int fs_write_cluster_chain(uint32_t start_cluster, const uint8_t *buffer, uint32_t offset, uint32_t size);
int fs_extend_file(uint32_t *start_cluster, uint32_t old_size, uint32_t new_size);
int fs_is_dir_empty(uint32_t dir_cluster);
int create_dir_entry(uint32_t dir_cluster, const char *name, uint8_t attr, uint32_t start_cluster, uint32_t size);

uint32_t get_fat_entry(uint32_t cluster);
int set_fat_entry(uint32_t cluster, uint32_t value);
int read_sector(uint32_t sector, uint8_t *buffer);
int write_sector(uint32_t sector, const uint8_t *buffer);
uint32_t cluster_to_sector(uint32_t cluster);

#endif

