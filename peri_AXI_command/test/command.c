#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#define PERI_CMD_BASEADDR 0xA0010000UL
#define MAP_SIZE          4096UL
#define MAP_MASK          (MAP_SIZE - 1)

#define REG_ROM_L     0
#define REG_ROM_H     1
#define REG_STROKE_L  2
#define REG_STROKE_H  3
#define REG_CMD       4

#define CMD_STICK1_SHIFT 0
#define CMD_STICK3_SHIFT 5
#define CMD_START_BIT    10
#define CMD_DONE_BIT     11

static void write_command(volatile uint32_t *reg,
                          uint16_t rom0, uint16_t rom1, uint16_t rom2, uint16_t rom3,
                          uint16_t stroke_right, uint16_t stroke_left,
                          uint16_t stroke_up, uint16_t stroke_down,
                          uint8_t stick1, uint8_t stick3)
{
    uint32_t cmd_reg;

    reg[REG_ROM_L] = ((uint32_t)rom1 << 16) | rom0;
    reg[REG_ROM_H] = ((uint32_t)rom3 << 16) | rom2;

    reg[REG_STROKE_L] = ((uint32_t)stroke_left << 16) | stroke_right;
    reg[REG_STROKE_H] = ((uint32_t)stroke_down << 16) | stroke_up;

    cmd_reg = ((uint32_t)(stick1 & 0x1F) << CMD_STICK1_SHIFT)
            | ((uint32_t)(stick3 & 0x1F) << CMD_STICK3_SHIFT);

    // cmd_from_PSを一度0にする
    reg[REG_CMD] = cmd_reg;
    usleep(1000);

    // 0→1の立ち上がりを作る
    reg[REG_CMD] = cmd_reg | (1U << CMD_START_BIT);
    usleep(1000);

    // 必要なら戻す
    reg[REG_CMD] = cmd_reg;
}

static int wait_done(volatile uint32_t *reg)
{
    uint32_t val;

    while (1) {
        val = reg[REG_CMD];

        if ((val >> CMD_DONE_BIT) & 0x1) {
            return 0;
        }

        usleep(1000);
    }
}

int main(void)
{
    int fd;
    void *map_base;
    volatile uint32_t *reg;
    char key;

    fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        perror("open /dev/mem");
        return 1;
    }

    map_base = mmap(NULL, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
                    fd, PERI_CMD_BASEADDR & ~MAP_MASK);

    if (map_base == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return 1;
    }

    reg = (volatile uint32_t *)((char *)map_base + (PERI_CMD_BASEADDR & MAP_MASK));

    printf("q: stroke right/left 2000\n");
    printf("w: stick_posi = 20, stroke = 0\n");
    printf("e: stick_posi = 15, stroke = 0\n");
    printf("x: exit\n");

    while (1) {
        printf("> ");
        scanf(" %c", &key);

        if (key == 'x') {
            break;
        }

        if (key == 'q') {
            printf("command: stick=15, stroke R/L=2000\n");
            write_command(reg,
                          1000, 1000, 1000, 1000,
                          2000, 2000, 0, 0,
                          15, 15);
            wait_done(reg);
            printf("done\n");
        }
        else if (key == 'w') {
            printf("command: stick=20, stroke=0\n");
            write_command(reg,
                          1000, 1000, 1000, 1000,
                          0, 0, 0, 0,
                          20, 20);
            wait_done(reg);
            printf("done\n");
        }
        else if (key == 'e') {
            printf("command: stick=15, stroke=0\n");
            write_command(reg,
                          1000, 1000, 1000, 1000,
                          0, 0, 0, 0,
                          15, 15);
            wait_done(reg);
            printf("done\n");
        }
        else {
            printf("unknown key\n");
        }
    }

    munmap(map_base, MAP_SIZE);
    close(fd);

    return 0;
}