#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#define ENCODER_BASE 0xA0000000
#define MAP_SIZE     0x10000

#define REG0_OFFSET  0x00
#define REG1_OFFSET  0x04

int main(void)
{
    int fd = open("/dev/mem", O_RDONLY | O_SYNC);
    if (fd < 0) {
        perror("open /dev/mem");
        return 1;
    }

    void *map_base = mmap(
        NULL,
        MAP_SIZE,
        PROT_READ,
        MAP_SHARED,
        fd,
        ENCODER_BASE
    );

    if (map_base == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return 1;
    }

    volatile uint32_t *reg = (volatile uint32_t *)map_base;

    printf("encoder realtime check\n");
    printf("Ctrl+C で終了\n\n");

    while (1) {
        uint32_t reg0 = reg[REG0_OFFSET / 4];
        uint32_t reg1 = reg[REG1_OFFSET / 4];

        uint32_t enc0 =  reg0        & 0x0FFF;
        uint32_t enc1 = (reg0 >> 12) & 0x0FFF;

        uint32_t enc2 =  reg1        & 0x0FFF;
        uint32_t enc3 = (reg1 >> 12) & 0x0FFF;

        printf("\renc0=%4u  enc1=%4u  enc2=%4u  enc3=%4u",
               enc0, enc1, enc2, enc3);

        fflush(stdout);
        usleep(50000); // 50ms
    }

    munmap(map_base, MAP_SIZE);
    close(fd);

    return 0;
}