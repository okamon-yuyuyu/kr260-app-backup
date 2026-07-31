#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>

#define PARAM_BASEADDR 0xA0020000
#define MAP_SIZE       4096UL
#define MAP_MASK       (MAP_SIZE - 1)

int main(void)
{
    int fd;
    void *map_base;
    volatile uint32_t *reg;

    uint32_t old_toggle;
    uint32_t new_toggle;

    // set parameters
    uint16_t rom0 = 0;
    uint16_t rom1 = 0;
    uint16_t rom2 = 1200;
    uint16_t rom3 = 1200;

    uint16_t stroke_up    = 0;
    uint16_t stroke_down  = 0;
    uint16_t stroke_right = 1500;
    uint16_t stroke_left  = 1500;


    static uint32_t toggle = 0;

    fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        printf("open failed: %s\n", strerror(errno));
        return 1;
    }

    map_base = mmap(
        NULL,
        MAP_SIZE,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        fd,
        PARAM_BASEADDR & ~MAP_MASK
    );

    if (map_base == MAP_FAILED) {
        printf("mmap failed: %s\n", strerror(errno));
        close(fd);
        return 1;
    }

    reg = (volatile uint32_t *)((char *)map_base + (PARAM_BASEADDR & MAP_MASK));
    /* ===== ROM parameter write ===== */

    reg[0] = ((uint32_t)rom1 << 16) | rom0;
    reg[1] = ((uint32_t)rom3 << 16) | rom2;

    /* ===== Stroke parameter write ===== */

    reg[2] = ((uint32_t)stroke_left << 16) | stroke_right;
    reg[3] = ((uint32_t)stroke_down << 16) | stroke_up;

    /* ===== Toggle update ===== */

    old_toggle = reg[4] & 0x1;

    new_toggle = old_toggle ^ 0x1;

    reg[4] = new_toggle;

    /* ===== Debug print ===== */

    printf("\n");
    printf("write done\n");
    printf("\n");

    printf("ROM\n");
    printf("rom0 = %d\n", rom0);
    printf("rom1 = %d\n", rom1);
    printf("rom2 = %d\n", rom2);
    printf("rom3 = %d\n", rom3);

    printf("\n");

    printf("STROKE\n");
    printf("right = %d\n", stroke_right);
    printf("left  = %d\n", stroke_left);
    printf("up    = %d\n", stroke_up);
    printf("down  = %d\n", stroke_down);

    printf("\n");

    printf("REGISTER\n");

    printf("reg0 = 0x%08X\n", reg[0]);
    printf("reg1 = 0x%08X\n", reg[1]);
    printf("reg2 = 0x%08X\n", reg[2]);
    printf("reg3 = 0x%08X\n", reg[3]);
    printf("reg4 = 0x%08X\n", reg[4]);

    printf("\n");

    printf("toggle : %u -> %u\n",
           old_toggle,
           new_toggle);

    munmap(map_base, MAP_SIZE);

    close(fd);

    return 0;
}