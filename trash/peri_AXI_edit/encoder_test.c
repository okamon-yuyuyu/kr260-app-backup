#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#define ENCODER_BASEADDR  0xA0000000
#define REG0_OFFSET       0x0
#define MAP_SIZE          4096UL
#define MAP_MASK          (MAP_SIZE - 1)

int main(void)
{
    int fd;
    void *map_base; 
    volatile uint32_t *reg; 

    printf("step1: start\n");
    fflush(stdout);

    fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        printf("open failed: %s\n", strerror(errno));
        fflush(stdout);
        return 1;
    }

    printf("step2: open ok\n");
    fflush(stdout);

    map_base = mmap(NULL, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
                    fd, ENCODER_BASEADDR & ~MAP_MASK);
    if (map_base == MAP_FAILED) {
        printf("mmap failed: %s\n", strerror(errno));
        fflush(stdout);
        close(fd);
        return 1;
    }

    printf("step3: mmap ok\n");
    fflush(stdout);

    reg = (volatile uint32_t *)((char *)map_base
          + (ENCODER_BASEADDR & MAP_MASK)
          + REG0_OFFSET);

    printf("map_base = %p\n", map_base);
    printf("reg addr = %p\n", reg);

    printf("step4: before read\n");
    fflush(stdout);

    uint32_t raw = *reg;

    printf("step5: read ok raw=0x%08X\n", raw);
    fflush(stdout);

    munmap(map_base, MAP_SIZE);
    close(fd);
    return 0;
}
