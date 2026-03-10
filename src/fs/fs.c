//
// Created by jwyoo on 26. 3. 10..
//

#include "fs.h"

#include <stdbool.h>
#include <stdint.h>
#include "fat.h"
#include "../drivers/ahci.h"
#include "../vga.h"
#include "../util/util.h"

// 전역 파일 시스템 객체
Fat g_root_fat;

// 1. void* -> uint8_t* 로 변경
bool ahci_disk_read(uint8_t* buf, uint32_t lba) {
    ahci_device_t* dev = ahci_get_device(0);
    if (!dev) return false;

    // ahci_read_sectors는 내부적으로 void*를 받으므로 여기서 넘기는 건 문제 없습니다.
    return ahci_read_sectors(dev, lba, 1, buf) == 1;
}

// 2. const void* -> const uint8_t* 로 변경
bool ahci_disk_write(const uint8_t* buf, uint32_t lba) {
    ahci_device_t* dev = ahci_get_device(0);
    if (!dev) return false;

    return ahci_write_sectors(dev, lba, 1, (void*)buf) == 1;
}

// 이제 타입이 완벽하게 일치하므로 Clangd 경고가 사라집니다.
static DiskOps ahci_disk_ops = {
    .read = ahci_disk_read,
    .write = ahci_disk_write
};

void init_fs(void) {
    kprintf("[FS] Initializing FAT32 File System...\n");

    if (ahci_get_device_count() == 0) {
        kprintf("[FS] Error: No AHCI devices found.\n");
        return;
    }

    int err = fat_mount(&ahci_disk_ops, 0, &g_root_fat, "0");

    if (err == FAT_ERR_NONE) {
        kprintf("[FS] FAT32 successfully mounted on '/'\n");
        kprintf("[FS] OEM Name: %s\n", g_root_fat.name);
    } else {
        kprintf("[FS] Failed to mount FAT32. Error: %s\n", fat_get_error(err));
    }
}
// src/fs/fs.c 에 추가

void fs_print_info(void) {
    if (g_root_fat.ops.read == NULL) {
        kprintf("No filesystem mounted.\n");
        return;
    }

    kprintf("\n--- FAT32 Volume Information ---\n");
    kprintf("Volume Name:      %s\n", g_root_fat.name);

    // 섹터 당 512바이트 기준, 클러스터 당 섹터 수를 계산
    uint32_t sectors_per_cluster = (1 << g_root_fat.clust_shift);
    kprintf("Bytes per Sector: 512\n");
    kprintf("Sectors per Clust:%d (%d bytes)\n", sectors_per_cluster, sectors_per_cluster * 512);

    kprintf("FAT 0 Sector:     %d\n", g_root_fat.fat_sect[0]);
    if (g_root_fat.fat_sect[1]) {
        kprintf("FAT 1 Sector:     %d (Mirroring Enabled)\n", g_root_fat.fat_sect[1]);
    }

    kprintf("Data Sector Start:%d\n", g_root_fat.data_sect);
    kprintf("Root Cluster:     %d\n", g_root_fat.root_clust);
    kprintf("Total Clusters:   %d\n", g_root_fat.clust_cnt);
    kprintf("Free Clusters:    %d\n", g_root_fat.free_cnt);

    // 전체 용량 계산 (MB 단위)
    uint32_t total_mb = (g_root_fat.clust_cnt * sectors_per_cluster * 512) / 1024 / 1024;
    kprintf("Approx. Capacity: %d MB\n", total_mb);
    kprintf("--------------------------------\n");
}
// src/fs/fs.c

// src/fs/fs.c 내의 fs_ls 함수 수정

// src/fs/fs.c

void fs_ls(const char* path) {
    Dir dir;
    DirInfo info;
    int err;

    err = fat_dir_open(&dir, path);
    if (err != FAT_ERR_NONE) {
        kprintf("ls: Cannot open %s (%s)\n", path, fat_get_error(err));
        return;
    }

    kprintf("Type  Size        Name\n");
    kprintf("----  ----------  ----------------\n");

    // 루프 내부에서 확실하게 다음 항목으로 넘어가도록 보장
    while ((err = fat_dir_read(&dir, &info)) == FAT_ERR_NONE) {
        char type = (info.attr & FAT_ATTR_DIR) ? 'D' : 'F';

        // info.name이 딱 이름 길이만큼만 출력되도록 0(NULL)을 강제로 삽입
        if (info.name_len < sizeof(info.name)) {
            info.name[info.name_len] = '\0';
        }

        kprintf("[%c]  %dB  %s\n", type, (uint32_t)info.size, info.name);
    }
}

void fs_cat(const char* path) {
    File file;
    char buf[512]; // 한 번에 읽을 버퍼 크기
    int bytes_read = 0;
    int err;

    // 1. 파일 열기 (읽기 모드)
    err = fat_file_open(&file, path, FAT_READ);
    if (err != FAT_ERR_NONE) {
        kprintf("cat: Cannot open %s (%s)\n", path, fat_get_error(err));
        return;
    }

    // 2. 파일 내용 읽기 및 출력
    // 파일 크기가 512바이트보다 크더라도 반복해서 읽습니다.
    while (file.offset < file.size) {
        err = fat_file_read(&file, buf, 511, &bytes_read);
        if (err != FAT_ERR_NONE && err != FAT_ERR_EOF) {
            kprintf("\ncat: Error reading file (%s)\n", fat_get_error(err));
            break;
        }

        if (bytes_read <= 0) break;

        // 문자열 출력을 위해 끝에 널 문자를 추가
        buf[bytes_read] = '\0';
        kprintf("%s", buf);
    }

    kprintf("\n"); // 출력 후 개행

    // 3. 파일 닫기
    fat_file_close(&file);
}
// src에서 읽어서 dest 파일로 쓰는 함수
void fs_redirect_to_file(const char* src_path, const char* dest_path) {
    File src_file, dest_file;
    char buf[512];
    int bytes_read;
    int bytes_written;

    // 1. 소스 파일 열기
    if (fat_file_open(&src_file, src_path, FAT_READ) != FAT_ERR_NONE) {
        kprintf("Redirect: Source file not found.\n");
        return;
    }

    // 2. 대상 파일 열기 (없으면 생성, 있으면 내용 삭제 후 새로 작성)
    if (fat_file_open(&dest_file, dest_path, FAT_CREATE | FAT_WRITE | FAT_TRUNC) != FAT_ERR_NONE) {
        kprintf("Redirect: Cannot create destination file.\n");
        fat_file_close(&src_file);
        return;
    }

    kprintf("Redirecting %s -> %s...\n", src_path, dest_path);

    // 3. 복사 루프
    while (src_file.offset < src_file.size) {
        fat_file_read(&src_file, buf, 512, &bytes_read);
        if (bytes_read <= 0) break;

        fat_file_write(&dest_file, buf, bytes_read, &bytes_written);
    }

    // 4. 동기화 및 닫기
    fat_file_close(&src_file);
    fat_file_close(&dest_file);
}
void fs_write_string(const char* dest_path, const char* str) {
    File dest_file;
    int bytes_written;

    if (fat_file_open(&dest_file, dest_path, FAT_CREATE | FAT_WRITE | FAT_TRUNC) != FAT_ERR_NONE) {
        kprintf("echo: Can't create file.\n");
        return;
    }

    fat_file_write(&dest_file, str, strlen(str), &bytes_written);
    fat_file_close(&dest_file);
}