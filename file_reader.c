#include "file_reader.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#define BLOCK_SIZE 512

struct disk_t* disk_open_from_file(const char* volume_file_name)
{
    if(volume_file_name==NULL)
    {
        errno=EFAULT;
        return NULL;
    }
    struct disk_t *pdisk=malloc(sizeof(struct disk_t));
    if(pdisk==NULL)
    {
        errno=ENOMEM;
        return NULL;
    }
    uint8_t *sector=malloc(BLOCK_SIZE);
    if(sector==NULL)
    {
        free(pdisk);
        errno=ENOMEM;
        return NULL;
    }
    FILE *f=fopen(volume_file_name,"rb");
    if(f==NULL)
    {
        free(pdisk);
        free(sector);
        errno=ENOENT;
        return NULL;
    }
    pdisk->file=f;
    pdisk->total_sectors=0;
    while(fread(sector,BLOCK_SIZE,1,f)==1)
    {
        pdisk->total_sectors++;
    }
    free(sector);
    return pdisk;
}

int disk_read(struct disk_t* pdisk, int32_t first_sector, void* buffer, int32_t sectors_to_read)
{
    if(pdisk==NULL || buffer==NULL)
    {
        errno=EFAULT;
        return -1;
    }
    if(first_sector<0 || (first_sector+sectors_to_read)>pdisk->total_sectors)
    {
        errno=ERANGE;
        return -1;
    }
    fseek(pdisk->file,first_sector*BLOCK_SIZE,SEEK_SET);
    fread(buffer,BLOCK_SIZE,sectors_to_read,pdisk->file);
    return sectors_to_read;
}

int disk_close(struct disk_t* pdisk)
{
    if(pdisk==NULL)
    {
        errno=EFAULT;
        return -1;
    }
    fclose(pdisk->file);
    free(pdisk);
    return 0;
}

struct volume_t* fat_open(struct disk_t* pdisk, uint32_t first_sector)
{
    if(pdisk==NULL)
    {
        errno=EFAULT;
        return NULL;
    }
    struct volume_t *volume=malloc(sizeof(struct volume_t));
    if(volume==NULL)
    {
        errno=ENOMEM;
        return NULL;
    }
    if(disk_read(pdisk,(int32_t)first_sector,&volume->boot_sector,1)==-1)
    {
        free(volume);
        return NULL;
    }
    if(volume->boot_sector.signature_value!=0xaa55 || volume->boot_sector.bytes_per_sector!=512
       || volume->boot_sector.number_of_fats!=2)
    {
        free(volume);
        errno=EINVAL;
        return NULL;
    }
    volume->table1=malloc(volume->boot_sector.size_of_fat*volume->boot_sector.bytes_per_sector);
    if(volume->table1==NULL)
    {
        free(volume);
        errno=ENOMEM;
        return NULL;
    }
    volume->table2=malloc(volume->boot_sector.size_of_fat*volume->boot_sector.bytes_per_sector);
    if(volume->table2==NULL)
    {
        free(volume->table1);
        free(volume);
        errno=ENOMEM;
        return NULL;
    }
    disk_read(pdisk,volume->boot_sector.size_of_reserved_area,volume->table1,volume->boot_sector.size_of_fat);
    disk_read(pdisk,volume->boot_sector.size_of_reserved_area+volume->boot_sector.size_of_fat,volume->table2,volume->boot_sector.size_of_fat);
    if(memcmp(volume->table1,volume->table2,volume->boot_sector.size_of_fat*volume->boot_sector.bytes_per_sector)!=0)
    {
        free(volume->table1);
        free(volume->table2);
        free(volume);
        errno=EINVAL;
        return NULL;
    }
    volume->roots=malloc(sizeof(struct root_directory_t)*volume->boot_sector.max_number_of_files);
    if(volume->roots==NULL)
    {
        fat_close(volume);
        errno=ENOMEM;
        return NULL;
    }
    disk_read(pdisk,volume->boot_sector.size_of_reserved_area+(2*volume->boot_sector.size_of_fat),volume->roots,
              (int32_t)((volume->boot_sector.max_number_of_files*sizeof(struct root_directory_t))/volume->boot_sector.bytes_per_sector));
    volume->pdisk=pdisk;
    return volume;
}

int fat_close(struct volume_t* pvolume)
{
    if(pvolume==NULL)
    {
        errno=EFAULT;
        return -1;
    }
    free(pvolume->table1);
    free(pvolume->table2);
    free(pvolume->roots);
    free(pvolume);
    return 0;
}

struct clusters_chain_t *get_chain_fat12(const void *buffer, size_t size, uint16_t first_cluster)
{
    if(buffer==NULL || size<1)
    {
        return NULL;
    }
    struct clusters_chain_t *chain=calloc(1,sizeof(struct clusters_chain_t));
    if(chain==NULL)
    {
        return NULL;
    }
    chain->clusters=calloc(1,sizeof(uint16_t));
    if(chain->clusters==NULL)
    {
        free(chain);
        return NULL;
    }
    *(chain->clusters)=first_cluster;
    chain->size=1;
    while(1)
    {
        uint8_t *wsk=(uint8_t*)buffer;
        if(first_cluster%2==0)
        {
            int index=first_cluster/2*3;
            wsk+=index;
            first_cluster=*((uint16_t*)wsk);
            first_cluster=first_cluster<<4;
            first_cluster=first_cluster>>4;
        }
        else
        {
            int index=first_cluster+first_cluster/2;
            wsk+=index;
            first_cluster=*((uint16_t*)wsk);
            first_cluster=first_cluster>>4;
        }
        if(first_cluster>=4088)
        {
            break;
        }
        uint16_t *new=realloc(chain->clusters,sizeof(uint16_t)*(chain->size+1));
        if(new==NULL)
        {
            free(chain->clusters);
            free(chain);
            return NULL;
        }
        chain->clusters=new;
        *(chain->clusters+(chain->size))=first_cluster;
        chain->size++;
    }
    return chain;
}

struct file_t* file_open(struct volume_t* pvolume, const char* file_name)
{
    if(pvolume==NULL || file_name==NULL)
    {
        errno=EFAULT;
        return NULL;
    }
    struct file_t *file=malloc(sizeof(struct file_t));
    if(file==NULL)
    {
        errno=ENOMEM;
        return NULL;
    }
    struct root_directory_t *entry=(struct root_directory_t*)pvolume->roots;
    char name[11];
    for(int i=0;i<11;i++)
    {
        name[i]=' ';
    }
    for(int i=0;i<11;i++)
    {
        if(file_name[i]=='\0')
        {
            break;
        }
        if(file_name[i]=='.')
        {
            i++;
            for(int j=8;j<11;j++)
            {
                if(file_name[i]=='\0')
                {
                    break;
                }
                name[j]=file_name[i];
                i++;
            }
            break;
        }
        name[i]=file_name[i];
    }
    int find=0;
    for(int i=0;i<pvolume->boot_sector.max_number_of_files;i++)
    {
        int dif=0;
        for(int j=0;j<11;j++)
        {
            if(entry->name[j]!=name[j])
            {
                dif++;
                break;
            }
        }
        if(dif==0)
        {
            int is_directory=(entry->file_attributes>>4)&1;
            if(is_directory==1)
            {
                free(file);
                errno=EISDIR;
                return NULL;
            }
            file->root=*entry;
            find++;
            break;
        }
        entry++;
    }
    if(find==0)
    {
        free(file);
        errno=ENOENT;
        return NULL;
    }
    file->pvolume=pvolume;
    file->chain=get_chain_fat12(pvolume->table1,pvolume->boot_sector.size_of_fat*pvolume->boot_sector.bytes_per_sector,
                                file->root.low_order);
    if(file->chain==NULL)
    {
        free(file);
        errno=ENOMEM;
        return NULL;
    }
    file->position=0;
    return file;
}

int file_close(struct file_t* stream)
{
    if(stream==NULL)
    {
        errno=EFAULT;
        return -1;
    }
    free(stream->chain->clusters);
    free(stream->chain);
    free(stream);
    return 0;
}

int32_t file_seek(struct file_t* stream, int32_t offset, int whence)
{
    if(stream==NULL)
    {
        errno=EFAULT;
        return -1;
    }
    if(whence==SEEK_SET)
    {
        stream->position=offset;
    }
    else if(whence==SEEK_CUR)
    {
        stream->position+=offset;
    }
    else if(whence==SEEK_END)
    {
        stream->position=(int32_t)stream->root.file_size+offset;
    }
    else
    {
        errno=EINVAL;
        return -1;
    }
    if(stream->position<0 || stream->position>(int32_t)stream->root.file_size)
    {
        errno=ENXIO;
        return -1;
    }
    return abs(offset);
}

size_t file_read(void *ptr, size_t size, size_t nmemb, struct file_t *stream)
{
    if(ptr==NULL || stream==NULL)
    {
        errno=EFAULT;
        return -1;
    }
    int size_of_cluster=stream->pvolume->boot_sector.sector_per_cluster*stream->pvolume->boot_sector.bytes_per_sector;
    char *cluster=malloc(size_of_cluster);
    if(cluster==NULL)
    {
        errno=ENOMEM;
        return -1;
    }
    char *wsk=ptr;
    size_t counter=0;
    size_t result=0;
    int cc=0;
    int index=stream->position/size_of_cluster;
    while(1)
    {
        if(stream->position+(unsigned int)1>stream->root.file_size || counter>=nmemb*size || result>nmemb)
        {
            break;
        }
        int32_t sector_number=stream->pvolume->boot_sector.size_of_reserved_area
                              +(stream->pvolume->boot_sector.number_of_fats*stream->pvolume->boot_sector.size_of_fat)
                              +(stream->pvolume->boot_sector.max_number_of_files*sizeof(struct root_directory_t)/stream->pvolume->boot_sector.bytes_per_sector)
                              +((stream->chain->clusters[index]-2)*stream->pvolume->boot_sector.sector_per_cluster);
        if(disk_read(stream->pvolume->pdisk,sector_number,cluster,stream->pvolume->boot_sector.sector_per_cluster)==-1)
        {
            errno=ERANGE;
            return -1;
        }
        for(int i=0;i<size_of_cluster;i++)
        {
            int cluster_index=stream->position%size_of_cluster;
            if(counter<size*nmemb)
            {
                if(stream->position+(unsigned int)1>stream->root.file_size)
                {
                    free(cluster);
                    return result;
                }
                wsk[counter]=cluster[cluster_index];
                counter++;
                cc++;
                if((size_t)cc==size)
                {
                    result++;
                    cc=0;
                }
                stream->position++;
            }
            else
            {
                free(cluster);
                return result;
            }
        }
        index++;
    }
    free(cluster);
    return result;
}

struct dir_t* dir_open(struct volume_t* pvolume, const char* dir_path)
{
    if(pvolume==NULL || dir_path==NULL)
    {
        errno=EFAULT;
        return NULL;
    }
    struct dir_t *open=malloc(sizeof(struct dir_t));
    if(open==NULL)
    {
        errno=ENOMEM;
        return NULL;
    }
    if(strcmp("\\",dir_path)!=0)
    {
        free(open);
        errno=ENOENT;
        return NULL;
    }
    open->roots=pvolume->roots;
    open->size=pvolume->boot_sector.max_number_of_files;
    open->position=0;
    open->empty=0;
    return open;
}

int dir_read(struct dir_t* pdir, struct dir_entry_t* pentry)
{
    if(pdir==NULL || pentry==NULL)
    {
        errno=EFAULT;
        return -1;
    }
    int find=0;
    struct root_directory_t *entry=(struct root_directory_t *)pdir->roots;
    while(pdir->position<pdir->size)
    {
        if(entry[pdir->position].name[0]!=0x00 && entry[pdir->position].name[0]!=(char)0xe5
        && (((entry[pdir->position].file_attributes)>>3)&1)==0)
        {
            if((pdir->empty==0 && entry[pdir->position].file_size!=0) || (pdir->empty==1 && entry[pdir->position].file_size==0))
            {
                find++;
            }
        }
        pdir->position++;
        if(pdir->position==pdir->size && pdir->empty==0)
        {
            pdir->empty=1;
            pdir->position=0;
        }
        if(find!=0)
        {
            break;
        }
    }
    entry+=pdir->position-1;
    if(find==0)
    {
        return 1;
    }
    for(int i=0;i<13;i++)
    {
        pentry->name[i]=' ';
    }
    int j=0;
    for(int i=0;i<11;i++)
    {
        if(entry->name[i]==' ')
        {
            continue;
        }
        if(i==8)
        {
            if(entry->name[i]==' ')
            {
                pentry->name[j]='\0';
                break;
            }
            else
            {
                pentry->name[j]='.';
                j++;
                pentry->name[j]=entry->name[i];
                j++;
                continue;
            }
        }
        pentry->name[j]=entry->name[i];
        j++;
    }
    for(int i=0;i<13;i++)
    {
        if(pentry->name[i]==' ')
        {
            pentry->name[i]='\0';
            break;
        }
    }
    pentry->size=entry->file_size;
    pentry->is_readonly=(entry->file_attributes)&1;
    pentry->is_hidden=((entry->file_attributes)>>1)&1;
    pentry->is_system=((entry->file_attributes)>>2)&1;
    pentry->is_directory=((entry->file_attributes)>>4)&1;
    pentry->is_archived=((entry->file_attributes)>>5)&1;
    return 0;
}

int dir_close(struct dir_t* pdir)
{
    if(pdir==NULL)
    {
        errno=EFAULT;
        return -1;
    }
    free(pdir);
    return 0;
}
