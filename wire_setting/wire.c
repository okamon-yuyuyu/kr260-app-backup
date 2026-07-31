// set_target_raw.c
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#define ENCODER_BASE      0xA0030000
#define WIRE_SETTING_BASE 0xA0020000
#define MAP_SIZE          0x10000

// ==============================
// ここを書き換える
// ==============================
#define TARGET_RAW0 2489
#define TARGET_RAW1 2136
#define TARGET_RAW2 416
#define TARGET_RAW3 3814
// ==============================

int main(void)
{
    if (TARGET_RAW0 > 4095 || TARGET_RAW1 > 4095 ||
        TARGET_RAW2 > 4095 || TARGET_RAW3 > 4095) {
        printf("target_rawは0〜4095にしてください\n");
        return 1;
    }

    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        perror("open /dev/mem");
        return 1;
    }

    // エンコーダ
    void *enc_map = mmap(NULL, MAP_SIZE, PROT_READ,
                         MAP_SHARED, fd, ENCODER_BASE);
    if (enc_map == MAP_FAILED) {
        perror("mmap encoder");
        close(fd);
        return 1;
    }

    // wire_setting
    void *wire_map = mmap(NULL, MAP_SIZE, PROT_READ | PROT_WRITE,
                          MAP_SHARED, fd, WIRE_SETTING_BASE);
    if (wire_map == MAP_FAILED) {
        perror("mmap wire_setting");
        munmap(enc_map, MAP_SIZE);
        close(fd);
        return 1;
    }

    volatile uint32_t *enc_reg  = (volatile uint32_t *)enc_map;
    volatile uint32_t *wire_reg = (volatile uint32_t *)wire_map;

    // target_rawを書き込み
    wire_reg[0] = TARGET_RAW0 & 0x0FFF;
    wire_reg[1] = TARGET_RAW1 & 0x0FFF;
    wire_reg[2] = TARGET_RAW2 & 0x0FFF;
    wire_reg[3] = TARGET_RAW3 & 0x0FFF;

    printf("target_raw set\n");
    printf("target_raw0 = %u\n", TARGET_RAW0);
    printf("target_raw1 = %u\n", TARGET_RAW1);
    printf("target_raw2 = %u\n", TARGET_RAW2);
    printf("target_raw3 = %u\n\n", TARGET_RAW3);

    printf("encoder realtime check\n");
    printf("Ctrl+Cで終了\n\n");

    while (1) {
        uint32_t reg0 = enc_reg[0];
        uint32_t reg1 = enc_reg[1];

        uint32_t enc0 =  reg0        & 0x0FFF;
        uint32_t enc1 = (reg0 >> 12) & 0x0FFF;
        uint32_t enc2 =  reg1        & 0x0FFF;
        uint32_t enc3 = (reg1 >> 12) & 0x0FFF;

        printf("\renc0=%4u  enc1=%4u  enc2=%4u  enc3=%4u",
               enc0, enc1, enc2, enc3);

        fflush(stdout);
        usleep(50000);
    }

    munmap(wire_map, MAP_SIZE);
    munmap(enc_map, MAP_SIZE);
    close(fd);

    return 0;
}