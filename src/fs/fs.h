//
// Created by jwyoo on 26. 3. 10..
//

#ifndef PARINOS_FS_H
#define PARINOS_FS_H

#include "fat.h"

// 전역 루트 파일 시스템 객체
// 다른 파일(예: 커널 메인)에서 이 변수를 통해 파일 읽기/쓰기를 수행할 수 있습니다.
extern Fat g_root_fat;

// 파일 시스템 초기화 및 마운트 함수
void init_fs(void);
void fs_print_info(void);
void fs_ls(const char* path);
void fs_cat(const char* path);
void fs_redirect_to_file(const char* src_path, const char* dest_path);
void fs_write_string(const char* dest_path, const char* str);

#endif //PARINOS_FS_H