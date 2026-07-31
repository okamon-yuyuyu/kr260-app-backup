// wire_encoder_check.c
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#define ENCODER_BASE    0xA0000000
#define WIRE_PARAM_BASE 0xA0010000
#define MAP_SIZE        0x10000

// ==============================
// ここを書き換える
// ==============================
#define DUTY0 200
#define DUTY1 200
#define DUTY2 80
#define DUTY3 80

#define TARGET_RAW0 3443
#define TARGET_RAW1 794
#define TARGET_RAW2 2150
#define TARGET_RAW3 2348

#define DEADBAND 5
// ==============================

int main(void)
{
    if (DUTY0 > 255 || DUTY1 > 255 || DUTY2 > 255 || DUTY3 > 255) {
        printf("dutyは0〜255にしてください\n");
        return 1;
    }

    if (TARGET_RAW0 > 4095 || TARGET_RAW1 > 4095 ||
        TARGET_RAW2 > 4095 || TARGET_RAW3 > 4095) {
        printf("target_rawは0〜4095にしてください\n");
        return 1;
    }

    if (DEADBAND > 63) {
        printf("deadbandは0〜63にしてください\n");
        return 1;
    }

    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        perror("open /dev/mem");
        return 1;
    }

    void *enc_map = mmap(NULL, MAP_SIZE, PROT_READ, MAP_SHARED, fd, ENCODER_BASE);
    if (enc_map == MAP_FAILED) {
        perror("mmap encoder");
        close(fd);
        return 1;
    }

    void *wire_map = mmap(NULL, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, WIRE_PARAM_BASE);
    if (wire_map == MAP_FAILED) {
        perror("mmap wire_parameter");
        munmap(enc_map, MAP_SIZE);
        close(fd);
        return 1;
    }

    volatile uint32_t *enc_reg  = (volatile uint32_t *)enc_map;
    volatile uint32_t *wire_reg = (volatile uint32_t *)wire_map;

    uint32_t duty_reg =
          (DUTY0 & 0xFF)
        | ((DUTY1 & 0xFF) << 8)
        | ((DUTY2 & 0xFF) << 16)
        | ((DUTY3 & 0xFF) << 24);

    wire_reg[0] = duty_reg;
    wire_reg[1] = (TARGET_RAW0 & 0x0FFF) | ((DEADBAND & 0x3F) << 12);
    wire_reg[2] = TARGET_RAW1 & 0x0FFF;
    wire_reg[3] = TARGET_RAW2 & 0x0FFF;
    wire_reg[4] = TARGET_RAW3 & 0x0FFF;

    printf("wire parameter set\n");
    printf("duty:   %u %u %u %u\n", DUTY0, DUTY1, DUTY2, DUTY3);
    printf("target: %u %u %u %u\n",
           TARGET_RAW0, TARGET_RAW1, TARGET_RAW2, TARGET_RAW3);
    printf("deadband: %u\n\n", DEADBAND);
    printf("encoder realtime check\n");
    printf("Ctrl+C で終了\n\n");

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