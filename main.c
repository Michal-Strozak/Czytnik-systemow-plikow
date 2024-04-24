#include "file_reader.h"
#include <stdio.h>

int main() {
    char* expected_names[16] = { "TURN.BIN", "KEEP.TXT", "SEND.TX", "PROVIDE.TX", "DESERT.TX", "SPOT.TXT", "WHETHER.BIN", "SIGNTAKE.BIN", "LOW", "NEED", "IMAGINE", "LOVE", "GONE", "SENSE", "PHRASE", "TENBELLC" };

    struct disk_t* disk = disk_open_from_file("test.img");

    struct volume_t* volume = fat_open(disk, 0);

    struct dir_t* pdir = dir_open(volume, "\\");

    for (int i = 0; i < 16; ++i)
    {
        struct dir_entry_t entry;
        int res = dir_read(pdir, &entry);
        if(res!=0)
        {
            printf("i: %d - Funkcja dir_read() niepoprawnie odczytala wpis z katalogu - %d\n",i,res);
            return 1;
        }
        printf(".....................................................\n");
        printf("%s\n",entry.name);
        printf("%s\n",expected_names[i]);
    }
    struct dir_entry_t entry;
    int res = dir_read(pdir, &entry);
    if(res==0)
    {
        printf("Funkcja dir_read() zwrocila niepoprawna wartosc, po odczytaniu wszystkich wpisow z katalogu powinna zwrocic %d, a zwrocila %d", 1, res);
    }

    dir_close(pdir);
    fat_close(volume);
    disk_close(disk);
    return 0;
}
