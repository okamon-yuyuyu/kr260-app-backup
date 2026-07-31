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
#define MAP_MASK          (MAP_SIZE - 1) // 下位12bitだけ取り出すマスク

int main(void)
{
    int fd; // /dev/memのファイルディスクリプタ
    void *map_base; // mmapした先頭のアドレス(ポインタ型)
    volatile uint32_t *reg; // エンコーダのレジスタへのポインタ

    printf("step1: start\n");

    // /dev/memを開く
    fd = open("/dev/mem", O_RDWR | O_SYNC);

    if (fd < 0) {
        printf("open failed: %s\n", strerror(errno));
        return 1;
    }

    printf("step2: open ok\n");

    // IPコアの物理アドレスをCコードから触れる仮想アドレスに変換してマッピング
    // map_baseはmappingされた先頭アドレスを指す
    map_base = mmap(NULL,
                    MAP_SIZE,
                    PROT_READ | PROT_WRITE,
                    MAP_SHARED,
                    fd,
                    ENCODER_BASEADDR & ~MAP_MASK);

    // mmapが失敗した場合はエラーを表示して終了
    if (map_base == MAP_FAILED) {
        printf("mmap failed: %s\n", strerror(errno));
        close(fd);
        return 1;
    }

    printf("step3: mmap ok\n");
    // mmapした4KB領域の中から、実際のIPレジスタ位置を計算し、32bitレジスタとして扱えるポインタを作る
    reg = (volatile uint32_t *)((char *)map_base
          + (ENCODER_BASEADDR & MAP_MASK)
          + REG0_OFFSET);

    printf("map_base = %p\n", map_base);
    printf("reg addr = %p\n", reg);

    // 初回値取得
    uint32_t raw0_init = reg[0]; // encoder0とencoder1の初期値が入った32bit値を取得
    uint32_t raw1_init = reg[1]; // encoder2とencoder3の初期値が入った32bit値を取得

    int32_t base0 = raw0_init & 0xFFFF; // 下位16bitがencoder0の初期値
    int32_t base1 = (raw0_init >> 16) & 0xFFFF; // 上位16bitがencoder1の初期値

    int32_t base2 = raw1_init & 0xFFFF; // 下位16bitがencoder2の初期値
    int32_t base3 = (raw1_init >> 16) & 0xFFFF; // 上位16bitがencoder3の初期値

    printf("\nBase values\n");
    printf("enc0=%d  enc1=%d  enc2=%d  enc3=%d\n",
           base0, base1, base2, base3);

    while (1)
    {
        uint32_t raw0 = reg[0];
        uint32_t raw1 = reg[1];

        int32_t enc0 = raw0 & 0xFFFF;
        int32_t enc1 = (raw0 >> 16) & 0xFFFF;

        int32_t enc2 = raw1 & 0xFFFF;
        int32_t enc3 = (raw1 >> 16) & 0xFFFF;

        int32_t diff0 = enc0 - base0;
        int32_t diff1 = enc1 - base1;

        int32_t diff2 = enc2 - base2;
        int32_t diff3 = enc3 - base3;

        printf("\r");
        printf("d0=%6d  d1=%6d  d2=%6d  d3=%6d",
               diff0,
               diff1,
               diff2,
               diff3);

        fflush(stdout);

        usleep(100000); // 100ms
    }

    munmap(map_base, MAP_SIZE); // マッピング解除
    close(fd); // ファイルディスクリプタを閉じる

    return 0;
}