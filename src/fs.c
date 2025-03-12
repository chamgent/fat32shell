#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include "fs.h"
#include "utils.h"

FSInfo fsinfo;
OpenFileEntry open_files[MAX_OPEN_FILES];

static FAT32BootSector bs;

extern char current_path[512];


int read_sector(uint32_t sector, uint8_t *buffer) {
    if (!fsinfo.fp) return -1;
    if (fseek(fsinfo.fp, sector * fsinfo.bytes_per_sector, SEEK_SET)!=0) return -1;
    if (fread(buffer, 1, fsinfo.bytes_per_sector, fsinfo.fp)!=fsinfo.bytes_per_sector) return -1;
    return 0;
}

int write_sector(uint32_t sector, const uint8_t *buffer) {
    if (!fsinfo.fp) return -1;
    if (fseek(fsinfo.fp, sector * fsinfo.bytes_per_sector, SEEK_SET)!=0) return -1;
    if (fwrite(buffer, 1, fsinfo.bytes_per_sector, fsinfo.fp)!=fsinfo.bytes_per_sector) return -1;

    return 0;
}

uint32_t cluster_to_sector(uint32_t cluster) {
    return (cluster - 2)*fsinfo.sectors_per_cluster + fsinfo.first_data_sector;
}

uint32_t get_fat_entry(uint32_t cluster) {
    uint32_t fat_offset = cluster *4;
    uint32_t sector_num = fsinfo.first_FAT_sector + (fat_offset / fsinfo.bytes_per_sector);
    uint32_t offset_in_sector = fat_offset % fsinfo.bytes_per_sector;

    uint8_t sector[512];
    if (read_sector(sector_num, sector)!=0) return EOC;
    uint32_t val = *((uint32_t*)&sector[offset_in_sector]) & 0x0FFFFFFF;
    return val;
}

int set_fat_entry(uint32_t cluster, uint32_t value) {
    uint32_t fat_offset = cluster *4;
    uint32_t sector_num = fsinfo.first_FAT_sector + (fat_offset / fsinfo.bytes_per_sector);
    uint32_t offset_in_sector = fat_offset % fsinfo.bytes_per_sector;

    uint8_t sector[512];
    if (read_sector(sector_num, sector)!=0) return -1;
    *((uint32_t*)&sector[offset_in_sector]) = value;
    if (write_sector(sector_num, sector)!=0) return -1;

    for (int i=1; i<fsinfo.num_FATs; i++) {
        uint32_t backup_sec = fsinfo.first_FAT_sector + i*fsinfo.FATSz32 + (fat_offset / fsinfo.bytes_per_sector);
        if (fseek(fsinfo.fp, backup_sec*fsinfo.bytes_per_sector, SEEK_SET)!=0) return -1;
        if (fwrite(sector, 1, fsinfo.bytes_per_sector, fsinfo.fp)!=fsinfo.bytes_per_sector) return -1;
    }

    return 0;
}


int fs_mount(const char *image_path) {
    memset(&fsinfo,0,sizeof(fsinfo));
    memset(open_files,0,sizeof(open_files));

    fsinfo.fp = fopen(image_path, "r+b");
    if (!fsinfo.fp) {
        perror("fopen");
        return -1;
    }
    strncpy(fsinfo.image_name, image_path, sizeof(fsinfo.image_name)-1);

    uint8_t sector[512];
    if (fread(sector,1,512,fsinfo.fp)!=512) {
        fclose(fsinfo.fp);
        return -1;
    }
    memcpy(&bs, sector, sizeof(FAT32BootSector));

    fsinfo.bytes_per_sector = bs.BPB_BytsPerSec;
    fsinfo.sectors_per_cluster = bs.BPB_SecPerClus;
    fsinfo.reserved_sector_count = bs.BPB_RsvdSecCnt;
    fsinfo.num_FATs = bs.BPB_NumFATs;
    fsinfo.FATSz32 = bs.BPB_FATSz32;
    fsinfo.root_cluster = bs.BPB_RootClus;

    if (bs.BPB_TotSec32!=0) fsinfo.tot_sec = bs.BPB_TotSec32;
    else fsinfo.tot_sec = bs.BPB_TotSec16;

    fseek(fsinfo.fp,0,SEEK_END);
    fsinfo.image_size_bytes = ftell(fsinfo.fp);
    fseek(fsinfo.fp,0,SEEK_SET);

    fsinfo.first_FAT_sector = fsinfo.reserved_sector_count;
    fsinfo.first_data_sector = fsinfo.reserved_sector_count + fsinfo.num_FATs * fsinfo.FATSz32;
    fsinfo.total_clusters = (fsinfo.tot_sec - fsinfo.first_data_sector)/fsinfo.sectors_per_cluster;
    fsinfo.cwd_cluster = fsinfo.root_cluster;

    return 0;
}

void fs_unmount() {
    if (fsinfo.fp) {
        fclose(fsinfo.fp);
        fsinfo.fp=NULL;
    }
}

int fs_info() {
    printf("position of root cluster: %u\n", fsinfo.root_cluster);
    printf("bytes per sector: %u\n", fsinfo.bytes_per_sector);
    printf("sectors per cluster: %u\n", fsinfo.sectors_per_cluster);
    printf("total # of clusters in data region: %u\n", fsinfo.total_clusters);
    printf("# of entries in one FAT: %u\n", (fsinfo.FATSz32 * (fsinfo.bytes_per_sector/4)));
    printf("size of image (in bytes): %llu\n",(unsigned long long)fsinfo.image_size_bytes);
    return 0;
}

int fs_find_entry_in_dir(uint32_t dir_cluster, const char *name, DirEntry *out_entry, uint32_t *out_sector, uint32_t *out_offset) {
    char upper_name[256];
    strncpy(upper_name, name, sizeof(upper_name)-1);
    upper_name[sizeof(upper_name)-1] = '\0';
    to_upper(upper_name);

    uint32_t cluster = dir_cluster;
    uint8_t buf[512];

    while (cluster < 0x0FFFFFF8) {
        for (int s=0; s<fsinfo.sectors_per_cluster; s++) {
            uint32_t sec = cluster_to_sector(cluster)+s;
            if (read_sector(sec, buf)!=0) return -1;
            for (int i=0; i<fsinfo.bytes_per_sector; i+=32) {
                DirEntry *entry = (DirEntry*)&buf[i];
                if (entry->DIR_Name[0] == 0x00) {
                    return -1;
                }
                if ((entry->DIR_Attr & ATTR_LONG_NAME)==ATTR_LONG_NAME || entry->DIR_Name[0]==0xE5) {
                    continue;
                }
                char fname[12];
                memcpy(fname, entry->DIR_Name, 11);
                fname[11]='\0';
                for (int k=10;k>=0;k--){
                    if (fname[k]==' '||fname[k]==0x20)
                        fname[k]='\0';
                    else break;
                }
                to_upper(fname);
                if (strcmp(fname, upper_name)==0) {
                    memcpy(out_entry, entry, sizeof(DirEntry));
                    *out_sector = sec;
                    *out_offset = i;
                    return 0;
                }
            }
        }
        cluster = get_fat_entry(cluster);
    }
    return -1;
}

bool fs_name_exists_in_dir(uint32_t dir_cluster, const char *name) {
    DirEntry e; uint32_t s,o;
    return (fs_find_entry_in_dir(dir_cluster,name,&e,&s,&o)==0);
}


int fs_cd(const char *dirname) {
    if (strcmp(dirname,".")==0) return 0;
    if (strcmp(dirname,"..")==0) {
        if (fsinfo.cwd_cluster == fsinfo.root_cluster) return 0;
        DirEntry entry; uint32_t sec, off;
        if (fs_find_entry_in_dir(fsinfo.cwd_cluster,"..",&entry,&sec,&off)==0) {
            uint32_t parent = ((uint32_t)entry.DIR_FstClusHI<<16)|entry.DIR_FstClusLO;
            if (parent==0) parent=fsinfo.root_cluster;
            fsinfo.cwd_cluster=parent;
        }
        return 0;
    } else {
        DirEntry entry; uint32_t sec, off;
        if (fs_find_entry_in_dir(fsinfo.cwd_cluster, dirname,&entry,&sec,&off)==0) {
            if (entry.DIR_Attr & ATTR_DIRECTORY) {
                uint32_t c = ((uint32_t)entry.DIR_FstClusHI<<16)|entry.DIR_FstClusLO;
                if (c==0) c=fsinfo.root_cluster;
                fsinfo.cwd_cluster=c;
                return 0;
            } else {
                print_error("Not a directory.");
                return -1;
            }
        } else {
            print_error("Directory does not exist.");
            return -1;
        }
    }
}

int fs_ls() {
    uint32_t cluster = fsinfo.cwd_cluster;
    uint8_t buf[512];
    while (cluster<0x0FFFFFF8) {
        for (int s=0; s<fsinfo.sectors_per_cluster; s++) {
            uint32_t sec = cluster_to_sector(cluster)+s;
            if (read_sector(sec, buf)!=0) return -1;
            for (int i=0; i<fsinfo.bytes_per_sector;i+=32) {
                DirEntry *e=(DirEntry*)&buf[i];
                if (e->DIR_Name[0]==0x00) return 0;
                if ((e->DIR_Attr & ATTR_LONG_NAME)==ATTR_LONG_NAME || e->DIR_Name[0]==0xE5) continue;
                char fname[12];
                memcpy(fname,e->DIR_Name,11);
                fname[11]='\0';
                for (int k=10;k>=0;k--) {
                    if (fname[k]==' '||fname[k]==0x20) fname[k]='\0';
                    else break;
                }
                
                if(e->DIR_Attr & ATTR_DIRECTORY) printf("\033[34m%s\033[0m    ",fname);
                else printf("%s    ",fname);
            }
        }
        cluster=get_fat_entry(cluster);
    }
    return 0;
}

int fs_mkdir(const char *dirname) {
    if (fs_name_exists_in_dir(fsinfo.cwd_cluster, dirname)) {
        print_error("Name already exists.");
        return -1;
    }
    uint32_t new_cluster;
    
    if (fs_allocate_cluster_chain(1,&new_cluster)!=0) {
        print_error("No space.");
        return -1;
    }

    uint8_t buf[512];
    memset(buf,0,512);
    for (int i=0;i<fsinfo.sectors_per_cluster;i++) {
        if (write_sector(cluster_to_sector(new_cluster)+i,buf)!=0) {
            print_error("Failed init dir cluster.");
            return -1;
        }
    }

    
    if (create_dir_entry(new_cluster,".",ATTR_DIRECTORY,new_cluster,0)!=0) {
        print_error("Failed to create '.'");
        return -1;
    }
    uint32_t parent = fsinfo.cwd_cluster;
    if (create_dir_entry(new_cluster,"..",ATTR_DIRECTORY,(parent==0?fsinfo.root_cluster:parent),0)!=0) {
        print_error("Failed to create '..'");
        return -1;
    }

    if (create_dir_entry(fsinfo.cwd_cluster, dirname, ATTR_DIRECTORY,new_cluster,0)!=0) {
        print_error("Failed to create directory entry in cwd.");
        return -1;
    }
    return 0;
}

int fs_creat(const char *filename) {
    if (fs_name_exists_in_dir(fsinfo.cwd_cluster, filename)) {
        print_error("Name already exists.");
        return -1;
    }
    if (create_dir_entry(fsinfo.cwd_cluster, filename, 0, 0, 0)!=0) {
        print_error("Failed to create file entry.");
        return -1;
    }
    return 0;
}


int fs_open(const char *filename, const char *flags) {
    DirEntry entry; uint32_t sec, off;
    if (fs_find_entry_in_dir(fsinfo.cwd_cluster, filename, &entry, &sec, &off) != 0) {
        print_error("File does not exist.");
        return -1;
    }
    if (entry.DIR_Attr & ATTR_DIRECTORY) {
        print_error("Cannot open directory.");
        return -1;
    }

    for (int i=0; i<MAX_OPEN_FILES; i++) {
        if (open_files[i].in_use && strcmp(open_files[i].name, filename) == 0) {
            print_error("File already opened.");
            return -1;
        }
    }

    char mode[4];
    if (parse_flags(flags, mode) != 0) {
        print_error("Invalid mode.");
        return -1;
    }

    int idx = -1;
    for (int i=0; i<MAX_OPEN_FILES; i++) {
        if (!open_files[i].in_use) { idx = i; break; }
    }
    if (idx < 0) {
        print_error("Too many open files.");
        return -1;
    }

    uint32_t c = ((uint32_t)entry.DIR_FstClusHI << 16) | entry.DIR_FstClusLO;
    open_files[idx].in_use = true;
    strncpy(open_files[idx].name, filename, MAX_NAME_LEN);
    open_files[idx].name[MAX_NAME_LEN] = '\0';
    open_files[idx].cluster = c;
    open_files[idx].size = entry.DIR_FileSize;
    open_files[idx].attr = entry.DIR_Attr;
    strcpy(open_files[idx].mode, mode);
    open_files[idx].offset = 0;
    open_files[idx].dir_entry_sector = sec;
    open_files[idx].dir_entry_offset = off;

    
    open_files[idx].path[0] = '\0'; 

    size_t path_cap = sizeof(open_files[idx].path);
    size_t cp_len = strlen(current_path);
    size_t fn_len = strlen(filename);

    
    if (cp_len + 1 + fn_len + 1 > path_cap) {
        
        print_error("Path too long, truncating file path.");
        
        open_files[idx].path[0] = '/';
        open_files[idx].path[1] = '\0';
        strncat(open_files[idx].path, filename, path_cap - 2);
    } else {
        
        if (strcmp(current_path, "/") == 0) {
            
            open_files[idx].path[0] = '/';
            open_files[idx].path[1] = '\0';
            strncat(open_files[idx].path, filename, path_cap - 2);
        } else {
            
            strncat(open_files[idx].path, current_path, path_cap - 1);
            strncat(open_files[idx].path, "/", path_cap - strlen(open_files[idx].path) - 1);
            strncat(open_files[idx].path, filename, path_cap - strlen(open_files[idx].path) - 1);
        }
    }

    return 0;
}


int fs_close(const char *filename) {
    for (int i=0;i<MAX_OPEN_FILES;i++) {
        if (open_files[i].in_use && strcmp(open_files[i].name,filename)==0){
            open_files[i].in_use=false;
            return 0;
        }
    }
    print_error("File not opened.");
    return -1;
}

int fs_lsof() {
    int count=0;
    for (int i=0;i<MAX_OPEN_FILES;i++){
        if (open_files[i].in_use) {
            
            printf("%d: %s %s %u %s\n", i, open_files[i].name, open_files[i].mode, open_files[i].offset, open_files[i].path);
            count++;
        }
    }
    if (count==0) printf("No files opened.\n");
    return 0;
}

int fs_size(const char *filename) {
    DirEntry e;uint32_t s,o;
    if (fs_find_entry_in_dir(fsinfo.cwd_cluster,filename,&e,&s,&o)!=0) {
        print_error("File not found.");
        return -1;
    }
    if (e.DIR_Attr & ATTR_DIRECTORY) {
        print_error("Is a directory.");
        return -1;
    }
    printf("%u\n",e.DIR_FileSize);
    return 0;
}

int fs_lseek(const char *filename, uint32_t offset) {
    for (int i=0;i<MAX_OPEN_FILES;i++){
        if (open_files[i].in_use && strcmp(open_files[i].name,filename)==0) {
            if (offset>open_files[i].size) {
                print_error("Offset larger than file size.");
                return -1;
            }
            open_files[i].offset=offset;
            return 0;
        }
    }
    print_error("File not opened or does not exist.");
    return -1;
}

int fs_read(const char *filename, uint32_t size) {
    int idx=-1;
    for (int i=0;i<MAX_OPEN_FILES;i++){
        if (open_files[i].in_use && strcmp(open_files[i].name,filename)==0){idx=i;break;}
    }
    if (idx<0) {
        print_error("File not opened or does not exist.");
        return -1;
    }
    if (!strchr(open_files[idx].mode,'r')) {
        print_error("Not opened for reading.");
        return -1;
    }

    if (open_files[idx].offset+size > open_files[idx].size)
        size = open_files[idx].size - open_files[idx].offset;

    uint8_t *buf = malloc(size+1);
    if (!buf) return -1;
    if (fs_read_cluster_chain(open_files[idx].cluster, buf, open_files[idx].offset,size)!=0) {
        free(buf);
        print_error("Read error.");
        return -1;
    }
    buf[size]='\0';
    printf("%s\n",buf);
    open_files[idx].offset+=size;
    free(buf);
    return 0;
}


int fs_write(const char *filename, const char *str) {
    int idx = -1;
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (open_files[i].in_use && strcmp(open_files[i].name, filename) == 0) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        print_error("File not opened or does not exist.");
        return -1;
    }
    if (!strchr(open_files[idx].mode, 'w')) {
        print_error("Not opened for writing.");
        return -1;
    }

    uint32_t len = (uint32_t)strlen(str);
    uint32_t old_size = open_files[idx].size;
    uint32_t new_offset = open_files[idx].offset + len;

    
    if (new_offset > open_files[idx].size) {
        if (fs_extend_file(&open_files[idx].cluster, open_files[idx].size, new_offset) != 0) {
            print_error("Extend file failed.");
            return -1;
        }
        if (open_files[idx].cluster < 2) {
            print_error("Invalid cluster after extend.");
            return -1;
        }
    }

    if (fs_write_cluster_chain(open_files[idx].cluster, (const uint8_t*)str, open_files[idx].offset, len) != 0) {
        print_error("Write error.");
        return -1;
    }

    open_files[idx].offset = new_offset;

    if (new_offset > old_size) {
        open_files[idx].size = new_offset;
    }

    if (new_offset > old_size) {
        uint8_t sec_buf[512];
        if (read_sector(open_files[idx].dir_entry_sector, sec_buf) != 0) {
            print_error("Update dir entry read error.");
            return -1;
        }

        DirEntry *d = (DirEntry*)&sec_buf[open_files[idx].dir_entry_offset];
        d->DIR_FileSize = open_files[idx].size;

        if (write_sector(open_files[idx].dir_entry_sector, sec_buf) != 0) {
            print_error("Update dir entry write error.");
            return -1;
        }
    }

    return 0;
}



int fs_rename(const char *oldname, const char *newname) {
    for (int i=0;i<MAX_OPEN_FILES;i++){
        if (open_files[i].in_use && strcmp(open_files[i].name,oldname)==0){
            print_error("File must be closed first.");
            return -1;
        }
    }

    DirEntry olde;uint32_t s,o;
    if (fs_find_entry_in_dir(fsinfo.cwd_cluster,oldname,&olde,&s,&o)!=0) {
        print_error("Old name does not exist.");
        return -1;
    }
    if (strcmp(oldname,".")==0||strcmp(oldname,"..")==0) {
        print_error("Cannot rename special directories.");
        return -1;
    }
    if (fs_name_exists_in_dir(fsinfo.cwd_cluster,newname)) {
        print_error("New name already exists.");
        return -1;
    }

    uint8_t sec_buf[512];
    if (read_sector(s,sec_buf)!=0) return -1;
    DirEntry *ent=(DirEntry*)&sec_buf[o];
    char formatted[11]; format_name_11(newname,formatted);
    memcpy(ent->DIR_Name,formatted,11);
    if (write_sector(s,sec_buf)!=0) return -1;

    return 0;
}

int fs_rm(const char *filename) {
    for (int i=0;i<MAX_OPEN_FILES;i++){
        if (open_files[i].in_use && strcmp(open_files[i].name,filename)==0){
            print_error("File is opened.");
            return -1;
        }
    }
    DirEntry e;uint32_t s,o;
    if (fs_find_entry_in_dir(fsinfo.cwd_cluster,filename,&e,&s,&o)!=0) {
        print_error("File does not exist.");
        return -1;
    }
    if (e.DIR_Attr & ATTR_DIRECTORY) {
        print_error("Is a directory.");
        return -1;
    }
    uint32_t c=((uint32_t)e.DIR_FstClusHI<<16)|e.DIR_FstClusLO;
    if (c!=0) fs_free_cluster_chain(c);

    uint8_t sec_buf[512];
    if (read_sector(s,sec_buf)!=0)return -1;
    sec_buf[o]=0xE5;
    if (write_sector(s,sec_buf)!=0)return -1;
    return 0;
}

int fs_rmdir(const char *dirname) {
    DirEntry e;uint32_t s,o;
    if (fs_find_entry_in_dir(fsinfo.cwd_cluster,dirname,&e,&s,&o)!=0) {
        print_error("Dir does not exist.");
        return -1;
    }
    if (!(e.DIR_Attr & ATTR_DIRECTORY)) {
        print_error("Not a directory.");
        return -1;
    }

    uint32_t c=((uint32_t)e.DIR_FstClusHI<<16)|e.DIR_FstClusLO;
    if (!fs_is_dir_empty(c)){
        print_error("Directory not empty.");
        return -1;
    }

    if (c!=fsinfo.root_cluster && c!=0) fs_free_cluster_chain(c);

    uint8_t sec_buf[512];
    if (read_sector(s,sec_buf)!=0)return -1;
    sec_buf[o]=0xE5;
    if (write_sector(s,sec_buf)!=0)return -1;

    return 0;
}

int fs_allocate_cluster_chain(uint32_t count, uint32_t *start_cluster) {
    for (uint32_t c=2; c<fsinfo.total_clusters+2; c++) {
        if (get_fat_entry(c)==0) {
            if (set_fat_entry(c,EOC)!=0) return -1; 
            *start_cluster = c;
            return 0;
        }
    }
    return -1;
}


int fs_free_cluster_chain(uint32_t start_cluster) {
    uint32_t c=start_cluster;
    while (c<0x0FFFFFF8 && c>=2) {
        uint32_t nxt = get_fat_entry(c);
        set_fat_entry(c,0);
        if (nxt==c||nxt==0||nxt>=0x0FFFFFF8) break;
        c=nxt;
    }
    return 0;
}

int fs_update_dir_entry(uint32_t sector, uint32_t offset, DirEntry *entry) {
    uint8_t buf[512];
    if (read_sector(sector,buf)!=0)return -1;
    memcpy(&buf[offset],entry,sizeof(DirEntry));
    if (write_sector(sector,buf)!=0)return -1;
    return 0;
}

int fs_read_cluster_chain(uint32_t start_cluster, uint8_t *buffer, uint32_t offset, uint32_t size) {
    uint32_t cluster = start_cluster;
    uint32_t bytes_per_cluster = fsinfo.sectors_per_cluster*fsinfo.bytes_per_sector;

    
    while (offset>=bytes_per_cluster && cluster<0x0FFFFFF8 && cluster>=2) {
        offset -= bytes_per_cluster;
        cluster=get_fat_entry(cluster);
    }

    uint8_t temp[512];
    uint32_t remain=size;
    uint32_t buf_pos=0;

    while (remain>0 && cluster<0x0FFFFFF8 && cluster>=2) {
        for (int s=0;s<fsinfo.sectors_per_cluster;s++) {
            if (read_sector(cluster_to_sector(cluster)+s,temp)!=0)return -1;
            uint32_t chunk_size=fsinfo.bytes_per_sector;
            if (offset>0) {
                if (offset>=chunk_size) {
                    offset-=chunk_size;
                    continue;
                } else {
                    uint32_t to_copy = chunk_size-offset;
                    if (to_copy>remain) to_copy=remain;
                    memcpy(&buffer[buf_pos], &temp[offset], to_copy);
                    buf_pos+=to_copy;
                    remain-=to_copy;
                    offset=0;
                }
            } else {
                uint32_t to_copy = (remain>chunk_size)?chunk_size:remain;
                memcpy(&buffer[buf_pos], temp, to_copy);
                buf_pos+=to_copy;
                remain-=to_copy;
            }
            if (remain==0) break;
        }
        cluster=get_fat_entry(cluster);
    }
    return 0;
}

int fs_write_cluster_chain(uint32_t start_cluster, const uint8_t *buffer, uint32_t offset, uint32_t size) {
    if (start_cluster < 2 || start_cluster >= 0x0FFFFFF8) {
        return -1; 
    }

    uint32_t cluster = start_cluster;
    uint32_t bytes_per_cluster = fsinfo.sectors_per_cluster * fsinfo.bytes_per_sector;

    while (offset >= bytes_per_cluster && cluster < 0x0FFFFFF8 && cluster >= 2) {
        offset -= bytes_per_cluster;
        cluster = get_fat_entry(cluster);
    }

    uint32_t remain = size;
    uint32_t buf_pos = 0;
    uint8_t temp[512];

    while (remain > 0 && cluster < 0x0FFFFFF8 && cluster >= 2) {
        for (int s=0; s<fsinfo.sectors_per_cluster; s++){
            if (read_sector(cluster_to_sector(cluster)+s,temp)!=0) return -1;
            uint32_t chunk_size = fsinfo.bytes_per_sector;
            if (offset > 0) {
                if (offset >= chunk_size) {
                    offset -= chunk_size;
                    continue;
                } else {
                    uint32_t to_copy = chunk_size - offset;
                    if (to_copy > remain) to_copy = remain;
                    memcpy(&temp[offset], &buffer[buf_pos], to_copy);
                    buf_pos += to_copy;
                    remain -= to_copy;
                    offset = 0;
                }
            } else {
                uint32_t to_copy = (remain > chunk_size) ? chunk_size : remain;
                memcpy(temp, &buffer[buf_pos], to_copy);
                buf_pos += to_copy;
                remain -= to_copy;
            }
            if (write_sector(cluster_to_sector(cluster)+s,temp)!=0) return -1;
            if (remain == 0) break;
        }
        if (remain > 0) {
            uint32_t nxt = get_fat_entry(cluster);
            if (nxt < 2 || nxt >= 0x0FFFFFF8) break; 
            cluster = nxt;
        }
    }

    return (remain == 0) ? 0 : -1;
}


int fs_extend_file(uint32_t *start_cluster, uint32_t old_size, uint32_t new_size) {
    uint32_t bytes_per_cluster = fsinfo.sectors_per_cluster * fsinfo.bytes_per_sector;
    uint32_t old_clusters = (old_size == 0) ? 0 : ((old_size - 1)/bytes_per_cluster + 1);
    uint32_t new_clusters = (new_size == 0) ? 0 : ((new_size - 1)/bytes_per_cluster + 1);

    if (new_clusters > old_clusters) {
        
        if (*start_cluster == 0 && new_clusters > 0) {
            uint32_t c;
            if (fs_allocate_cluster_chain(1, &c) != 0) {
                return -1;
            }
            if (c < 2) return -1; 
            *start_cluster = c;
            old_clusters = 1;
        }

       
        uint32_t last = *start_cluster;
        while (last >= 2 && last < 0x0FFFFFF8 && get_fat_entry(last) != EOC) {
            last = get_fat_entry(last);
        }

       
        for (uint32_t i = old_clusters; i < new_clusters; i++) {
            uint32_t c;
            if (fs_allocate_cluster_chain(1, &c)!=0) return -1;
            if (c < 2) return -1;
            if (last != EOC && last >= 2) {
                if (set_fat_entry(last,c)!=0) return -1;
            }
            if (set_fat_entry(c,EOC)!=0) return -1;
            last = c;
        }
    }
    return 0;
}




int fs_is_dir_empty(uint32_t dir_cluster) {
    
    uint32_t cluster=dir_cluster;
    uint8_t buf[512];
    int entry_count=0;
    while (cluster<0x0FFFFFF8 && cluster>=2) {
        for (int sec=0; sec<fsinfo.sectors_per_cluster;sec++){
            uint32_t sc=cluster_to_sector(cluster)+sec;
            if (read_sector(sc,buf)!=0)return 1;
            for (int i=0;i<fsinfo.bytes_per_sector;i+=32){
                DirEntry *d=(DirEntry*)&buf[i];
                if (d->DIR_Name[0]==0x00) return (entry_count<=2);
                if ((d->DIR_Attr & ATTR_LONG_NAME)==ATTR_LONG_NAME || d->DIR_Name[0]==0xE5) continue;
                entry_count++;
                if (entry_count>2) return 0; 
            }
        }
        cluster=get_fat_entry(cluster);
    }
    return 1; 
}


int create_dir_entry(uint32_t dir_cluster, const char *name, uint8_t attr, uint32_t start_cluster, uint32_t size) {
    DirEntry newe;
    memset(&newe, 0, sizeof(newe));
    format_name_11(name, (char*)newe.DIR_Name);
    newe.DIR_Attr = attr;
    newe.DIR_FileSize = size;
    newe.DIR_FstClusHI = (uint16_t)(start_cluster >> 16);
    newe.DIR_FstClusLO = (uint16_t)(start_cluster & 0xFFFF);

    uint32_t cluster = dir_cluster;
    uint8_t buf[512];

    while (cluster < 0x0FFFFFF8) {
        for (int s = 0; s < fsinfo.sectors_per_cluster; s++) {
            uint32_t sec = cluster_to_sector(cluster) + s;
            if (read_sector(sec, buf) != 0) return -1;

            for (int i = 0; i < fsinfo.bytes_per_sector; i += 32) {
                DirEntry *d = (DirEntry*)&buf[i];
                if (d->DIR_Name[0] == 0x00 || d->DIR_Name[0] == 0xE5) {
                    
                    memcpy(d, &newe, sizeof(DirEntry));
                    if (write_sector(sec, buf) != 0) return -1;
                    return 0;
                }
            }
        }

        
        uint32_t nxt = get_fat_entry(cluster);
        if (nxt >= 0x0FFFFFF8) {
            
            uint32_t c;
            if (fs_allocate_cluster_chain(1, &c) != 0) return -1;
            if (set_fat_entry(cluster, c) != 0) return -1;
            if (set_fat_entry(c, 0x0FFFFFFF) != 0) return -1; 

            
            memset(buf, 0, 512);
            for (int i = 0; i < fsinfo.sectors_per_cluster; i++) {
                if (write_sector(cluster_to_sector(c) + i, buf) != 0) return -1;
            }

            
            uint32_t sec = cluster_to_sector(c);
            if (read_sector(sec, buf) != 0) return -1;

            DirEntry *d = (DirEntry*)&buf[0];
            memcpy(d, &newe, sizeof(DirEntry));

            if (write_sector(sec, buf) != 0) return -1;
            return 0;
        }
        cluster = nxt;
    }

    return -1; 
}

