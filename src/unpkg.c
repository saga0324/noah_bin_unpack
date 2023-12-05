#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define PKG_HEADER_SIZE 2048
#define NUM_ITEMS 31
#define OUTPUT_DIR_SIZE 1024
#define OUTPUT_PATH_SIZE 2048
#define DEV_NAME_SIZE 12
#define FSTYPE_NAME_SIZE 13

static uint32_t crc32_table[256];

void generate_crc32_table()
{
    uint32_t polynomial = 0xEDB88320;
    for (uint32_t i = 0; i < 256; i++)
    {
        uint32_t crc = i;
        for (uint8_t j = 0; j < 8; j++)
        {
            crc = (crc & 1) ? (crc >> 1) ^ polynomial : crc >> 1;
        }
        crc32_table[i] = crc;
    }
}

uint32_t calculate_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++)
    {
        crc = (crc >> 8) ^ crc32_table[(crc ^ data[i]) & 0xFF];
    }
    return ~crc;
}

typedef struct
{
    char *name;
    int32_t id;
} PkgFSType;

static PkgFSType pkg_fstype_tbl[] = {
    {"none", 0}, {"fat", 1}, {"yaffs", 2}, {"yaffs2", 3}, {"ext2", 4}, {"ram", 5}, {"raw", 6}, {"nor", 7}, {"ubifs", 8}, {NULL, 0}};

typedef struct
{
    uint32_t len;
    uint32_t offset;
    int32_t ver;
    int32_t fstype;
    uint32_t checksum;
    char dev[DEV_NAME_SIZE];
    char unset[32];
} PkgItem;

typedef struct
{
    int64_t tag;
    int32_t ver;
    char unset[52];
    PkgItem item[NUM_ITEMS];
} PkgFileHeader;

PkgFileHeader pkg_file_header;

int ora_buf(char *buffer, int size)
{
    for (int i = 0; i < size; ++i)
    {
        buffer[i] = ((buffer[i] & 0x55) << 1) | ((buffer[i] & 0xAA) >> 1);
    }
    return size;
}

int process_file(const char *input_path, const char *output_directory)
{
    FILE *upgrade_stream = fopen(input_path, "rb");
    if (!upgrade_stream)
    {
        perror("[ERROR] File Read Error");
        return EXIT_FAILURE;
    }

    if (fread(&pkg_file_header, PKG_HEADER_SIZE, 1, upgrade_stream) != 1)
    {
        perror("[ERROR] Header Read Error");
        fclose(upgrade_stream);
        return EXIT_FAILURE;
    }
    fclose(upgrade_stream);

    ora_buf((char *)&pkg_file_header, PKG_HEADER_SIZE);

    int upgrade_fd = open(input_path, O_RDONLY);
    if (upgrade_fd == -1)
    {
        perror("[ERROR] File Open Error");
        return EXIT_FAILURE;
    }

    struct stat statbuff;
    if (fstat(upgrade_fd, &statbuff) == -1)
    {
        perror("[ERROR] File Stat Error");
        close(upgrade_fd);
        return EXIT_FAILURE;
    }

    void *upgrade_mmap = mmap(NULL, statbuff.st_size, PROT_READ, MAP_PRIVATE, upgrade_fd, 0);
    if (upgrade_mmap == MAP_FAILED)
    {
        perror("[ERROR] File Mapping Error");
        close(upgrade_fd);
        return EXIT_FAILURE;
    }

    generate_crc32_table();

    for (int i = 0; i < NUM_ITEMS; ++i)
    {
        if (pkg_file_header.item[i].len)
        {
            const uint8_t *data = (const uint8_t *)(upgrade_mmap + pkg_file_header.item[i].offset);
            uint32_t crc = calculate_crc32(data, pkg_file_header.item[i].len);
            pkg_file_header.item[i].checksum = crc;
            printf("\npkg_item_len = %d ", pkg_file_header.item[i].len);
            printf("\noffset = %d ", pkg_file_header.item[i].offset);
            printf("\nver = %d ", pkg_file_header.item[i].ver);
            char *fstype = "Unknown";
            for (int ii = 0; pkg_fstype_tbl[ii].name; ++ii)
            {
                if (pkg_fstype_tbl[ii].id == pkg_file_header.item[i].fstype)
                {
                    fstype = pkg_fstype_tbl[ii].name;
                    break;
                }
            }
            printf("\nfstype = %s ", fstype);
            printf("\nchecksum = 0x%08X ", pkg_file_header.item[i].checksum);
            printf("\ndev = ");
            fwrite(pkg_file_header.item[i].dev, 12, 1, stdout);

            char output_name[1024] = {0};
            if (pkg_file_header.item[i].dev[0] >= '0' && pkg_file_header.item[i].dev[0] <= '9')
            {
                char dev[13];
                memcpy(dev, pkg_file_header.item[i].dev, 12);
                dev[12] = '\0';
                long devi = strtol(dev, 0, 0);
                if (devi == 0)
                    strcpy(output_name, "u-boot-nand.bin");
                else if (devi == 0x400000 || devi == 0x500000)
                    strcpy(output_name, "uImage");
            }
            else if (!strcmp(pkg_file_header.item[i].dev, "/dev/null"))
            {
                strcpy(output_name, "uImage-initrd");
            }
            else
            {
                char *dev_filename = strrchr(pkg_file_header.item[i].dev, '/');
                if (dev_filename)
                {
                    dev_filename++;
                    if (!strcmp(dev_filename, "mtd3"))
                        dev_filename = "rootfs";
                    else if (!strcmp(dev_filename, "mtd4"))
                        dev_filename = "Settings";
                    else if (!strcmp(dev_filename, "mtd5"))
                        dev_filename = "ProgFS";
                    else if (!strcmp(dev_filename, "mtd6"))
                        dev_filename = "DataFS";
                    else if (!strcmp(dev_filename, "mtd7"))
                        dev_filename = "UsrFS";
                    else if (!strcmp(dev_filename, "mtd8"))
                        dev_filename = "UsrDisk";
                    else if (!strcmp(dev_filename, "ubi0_0"))
                        dev_filename = "rootfs";
                    else if (!strcmp(dev_filename, "ubi0_1"))
                        dev_filename = "Settings";
                    else if (!strcmp(dev_filename, "ubi0_2"))
                        dev_filename = "ProgFS";
                    else if (!strcmp(dev_filename, "ubi0_3"))
                        dev_filename = "DataFS";
                    else if (!strcmp(dev_filename, "ubi0_6"))
                        dev_filename = "UsrDisk";
                    sprintf(output_name, "%s.%s", dev_filename, fstype);
                }
            }
            if (output_name[0] == '\0')
                sprintf(output_name, "idx-%d-file.bin", i);
            printf("\nfile = %s ", output_name);
            printf("\n");

            char output_path[1024];
            snprintf(output_path, sizeof(output_path), "%s/%s", output_directory, output_name);

            FILE *s = fopen(output_path, "wb");
            fwrite(upgrade_mmap + pkg_file_header.item[i].offset, pkg_file_header.item[i].len, 1, s);
            fclose(s);
        }
    }

    munmap(upgrade_mmap, statbuff.st_size);
    close(upgrade_fd);
    return EXIT_SUCCESS;
}

int main(int argc, const char **argv)
{
    if (argc <= 2)
    {
        printf("Noah Upgrade Binary Unpacker v1.3\n");
        printf("Usage: %s <path/to/upgrade.bin> <output_directory>\n", argv[0]);
        return EXIT_FAILURE;
    }

    char output_directory[OUTPUT_DIR_SIZE];
    strncpy(output_directory, argv[2], OUTPUT_DIR_SIZE - 1);
    output_directory[OUTPUT_DIR_SIZE - 1] = '\0';

    struct stat st;
    if (stat(output_directory, &st) == -1)
    {
        if (mkdir(output_directory, 0700) == -1)
        {
            perror("[ERROR] Directory Creation Error");
            return EXIT_FAILURE;
        }
    }

    return process_file(argv[1], output_directory);
}
