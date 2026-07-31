#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#define RECEIVER_BASEADDR  0xA0010000
#define REG0_OFFSET       0x0
#define MAP_SIZE          4096UL
#define MAP_MASK          (MAP_SIZE - 1) // 下位12bitだけ取り出すマスク

int main(void)
{
    int fd; // /dev/memのファイルディスクリプタ
    void *map_base; // mmapした先頭のアドレス(ポインタ型)
    volatile uint32_t *reg; // receiverのレジスタへのポインタ

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
                    RECEIVER_BASEADDR & ~MAP_MASK);

    // mmapが失敗した場合はエラーを表示して終了
    if (map_base == MAP_FAILED) {
        printf("mmap failed: %s\n", strerror(errno));
        close(fd);
        return 1;
    }

    printf("step3: mmap ok\n");
    // mmapした4KB領域の中から、実際のIPレジスタ位置を計算し、32bitレジスタとして扱えるポインタを作る
    reg = (volatile uint32_t *)((char *)map_base
          + (RECEIVER_BASEADDR & MAP_MASK)
          + REG0_OFFSET);

    printf("map_base = %p\n", map_base);
    printf("reg addr = %p\n", reg);

    while (1)
    {
        uint32_t raw0 = reg[0];

        int32_t stick_posi_1 = raw0 & 0x1F;
        int32_t stick_posi_3 = (raw0 >> 5) & 0x1F;
        int32_t stick_high   = (raw0 >> 10) & 0x1;


        printf("\r");
        printf("raw0=0x%08X  stick1=%2d  stick3=%2d  high=%d",
               raw0,
               stick_posi_1,
               stick_posi_3,
               stick_high);

        fflush(stdout);

        usleep(100000); // 100ms
    }

    munmap(map_base, MAP_SIZE); // マッピング解除
    close(fd); // ファイルディスクリプタを閉じる

    return 0;
}