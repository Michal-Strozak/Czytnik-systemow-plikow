#ifndef FILE_READER_H
#define FILE_READER_H
#include <stdio.h>
#include <stdint.h>

struct disk_t{
    FILE* file;
    int32_t total_sectors;
};

struct boot_sector_t{
    char assembly_code[3];
    char name[8];
    uint16_t bytes_per_sector;
    uint8_t sector_per_cluster;
    uint16_t size_of_reserved_area;
    uint8_t number_of_fats;
    uint16_t max_number_of_files;
    uint16_t number_of_sectors;
    uint8_t media_type;
    uint16_t size_of_fat;
    uint16_t sectors_per_track;
    uint16_t number_of_heads;
    uint32_t number_of_sectors_before_partition;
    uint32_t number_of_sector_in_file_system;
    uint8_t drive_number;
    uint8_t not_used;
    uint8_t boot_signature;
    uint32_t volume_serial_number;
    char volume_label[11];
    char file_system_type_level[8];
    uint8_t unused[448];
    uint16_t signature_value; // 0xaa55
}__attribute__((__packed__));

struct root_directory_t{
    char name[11];
    uint8_t file_attributes;
    uint8_t reserved;
    uint8_t file_creation_time;
    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t access_date;
    uint16_t high_order;
    uint16_t modified_time;
    uint16_t modified_date;
    uint16_t low_order;
    uint32_t file_size;
}__attribute__((__packed__));

struct volume_t{
    struct disk_t *pdisk;
    struct boot_sector_t boot_sector;
    uint8_t *table1;
    uint8_t *table2;
    uint8_t *roots;
};

struct disk_t* disk_open_from_file(const char* volume_file_name);

int disk_read(struct disk_t* pdisk, int32_t first_sector, void* buffer, int32_t sectors_to_read);

int disk_close(struct disk_t* pdisk);

struct volume_t* fat_open(struct disk_t* pdisk, uint32_t first_sector);

int fat_close(struct volume_t* pvolume);

struct clusters_chain_t {
    uint16_t *clusters;
    size_t size;
};

struct clusters_chain_t *get_chain_fat12(const void *buffer, size_t size, uint16_t first_cluster);

struct file_t{
    struct volume_t *pvolume;
    struct root_directory_t root;
    struct clusters_chain_t *chain;
    int32_t position;
};

struct file_t* file_open(struct volume_t* pvolume, const char* file_name);

int file_close(struct file_t* stream);

size_t file_read(void *ptr, size_t size, size_t nmemb, struct file_t *stream);

int32_t file_seek(struct file_t* stream, int32_t offset, int whence);

struct dir_t{
    uint8_t *roots;
    int position;
    int size;
    int empty;
};

struct dir_entry_t{
    char name[13];
    size_t size;
    int is_archived;
    int is_readonly;
    int is_system;
    int is_hidden;
    int is_directory;
};

struct dir_t* dir_open(struct volume_t* pvolume, const char* dir_path);

int dir_read(struct dir_t* pdir, struct dir_entry_t* pentry);

int dir_close(struct dir_t* pdir);

#endif
