//
// src/fs/vfs_fat.h — FAT32 → VFS 어댑터
//
// 기존 FAT32 드라이버를 VFS 인터페이스에 등록하기 위한 어댑터입니다.
//

#ifndef PARINOS_VFS_FAT_H
#define PARINOS_VFS_FAT_H

#include "vfs.h"
#include "fat.h"

/**
 * FAT32 드라이버를 VFS 에 등록합니다.
 *
 * @param fat  이미 fat_mount() 로 초기화된 Fat 구조체 포인터
 * @param mp   마운트 포인트 (예: "/")
 * @return VFS_OK 또는 음수 에러 코드
 */
int vfs_fat_register(Fat *fat, const char *mp);

#endif /* PARINOS_VFS_FAT_H */
