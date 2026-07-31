// wire_set.c
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#define WIRE_PARAM_BASE 0xA0010000
#define MAP_SIZE        0x10000

#define REG_DUTY        0x00

int main(int argc, char *argv[])
{
    if (argc != 5) {
        printf("使い方:\n");
        printf("  sudo ./wire_set duty0 duty1 duty2 duty3\n");
        printf("例:\n");
        printf("  sudo ./wire_set 80 80 80 80\n");
        return 1;
    }

    uint32_t duty0 = atoi(argv[1]);
    uint32_t duty1 = atoi(argv[2]);
    uint32_t duty2 = atoi(argv[3]);
    uint32_t duty3 = atoi(argv[4]);

    if (duty0 > 255 || duty1 > 255 || duty2 > 255 || duty3 > 255) {
        printf("dutyは0〜255で指定してください\n");
        return 1;
    }

    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        perror("open /dev/mem");
        return 1;
    }

    void *map_base = mmap(
        NULL,
        MAP_SIZE,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        fd,
        WIRE_PARAM_BASE
    );

    if (map_base == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return 1;
    }

    volatile uint32_t *reg = (volatile uint32_t *)map_base;

    uint32_t duty_reg =
          (duty0 & 0xFF)
        | ((duty1 & 0xFF) << 8)
        | ((duty2 & 0xFF) << 16)
        | ((duty3 & 0xFF) << 24);

    reg[REG_DUTY / 4] = duty_reg;

    printf("duty set:\n");
    printf("  duty0 = %u\n", duty0);
    printf("  duty1 = %u\n", duty1);
    printf("  duty2 = %u\n", duty2);
    printf("  duty3 = %u\n", duty3);
    printf("  reg0  = 0x%08X\n", duty_reg);

    munmap(map_base, MAP_SIZE);
    close(fd);

    return 0;
}