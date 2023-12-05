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

static uint32_t crc32_table[256];

void generate_crc32_table()
{
    uint32_t polynomial = 0xEDB88320;
    for (uint32_t i = 0; i < 256; i++)
    {
        uint32_t crc = i;
        for (uint8_t j = 0; j < 8; j++)
        {
            if (crc & 1)
            {
                crc = (crc >> 1) ^ polynomial;
            }
            else
            {
                crc >>= 1;
            }
        }
        crc32_table[i] = crc;
    }
}

uint32_t calculate_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++)
    {
        uint8_t byte = data[i];
        uint32_t lookup_idx = (crc ^ byte) & 0xFF;
        crc = (crc >> 8) ^ crc32_table[lookup_idx];
    }
    return ~crc;
}

struct
{
    char *name;
    int32_t id;
} pkg_fstype_tbl[] = {
    {"none", 0},
    {"fat", 1},
    {"yaffs", 2},
    {"yaffs2", 3},
    {"ext2", 4},
    {"ram", 5},
    {"raw", 6},
    {"nor", 7},
    {"ubifs", 8},
    {NULL, 0},
};

struct
{
    int64_t tag;
    int32_t ver;
    char unset[52];
    struct
    {
        uint32_t len;
        uint32_t offset;
        int32_t ver;
        int32_t fstype;
        uint32_t checksum;
        char dev[12];
        char unset[32];
    } item[NUM_ITEMS];
} pkg_file_header;

int ora_buf(char *buffer, int size)
{
    int i;
    for (i = 0; i < size; ++i)
    {
        buffer[i] = ((buffer[i] & 0x55) << 1) | ((buffer[i] & 0xAA) >> 1);
    }
    return i;
}

int main(int argc, const char **argv)
{
    if (argc <= 1)
    {
        printf("Noah Upgrade Binary Unpacker v1.2\n");
        printf("Usage: %s <path/to/upgrade.bin> <output_directory>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char output_directory[1024];
    char output_path[2048];
    strncpy(output_directory, argv[2], sizeof(output_directory));
    output_directory[sizeof(output_directory) - 1] = '\0';

    FILE *upgrade_stream = fopen(argv[1], "rb");
    if (!upgrade_stream)
    {
        perror("[ERROR]File Read Error");
        return EXIT_FAILURE;
    }
    fread(&pkg_file_header, PKG_HEADER_SIZE, 1, upgrade_stream);
    fclose(upgrade_stream);

    ora_buf((char *)&pkg_file_header, PKG_HEADER_SIZE);

    int upgrade_fd = open(argv[1], O_RDONLY);
    struct stat statbuff;
    if (fstat(upgrade_fd, &statbuff) == -1)
    {
        perror("[ERROR]File Stat Error");
        close(upgrade_fd);
        return EXIT_FAILURE;
    }

    void *upgrade_mmap = mmap(NULL, statbuff.st_size, PROT_READ, MAP_PRIVATE, upgrade_fd, 0);
    if (upgrade_mmap == MAP_FAILED)
    {
        perror("[ERROR]File Map Error");
        close(upgrade_fd);
        return EXIT_FAILURE;
    }

    struct stat st = {0};
    if (stat(output_directory, &st) == -1)
    {
        mkdir(output_directory, 0700);
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
