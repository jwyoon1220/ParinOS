//
// Created by jwyoo on 26. 3. 12..
//

#ifndef PARINOS_ELF_H
#define PARINOS_ELF_H

//
// Created by jwyoo on 26. 3. 12..
//

#include <stdint.h>
#include <stdbool.h>

// 올려주신 FAT32 드라이버의 File 구조체를 사용하기 위해 인클루드
#include "../fs/fat.h"

// --- ELF 상수 및 매직 넘버 ---
#define ELF_MAGIC       0x464C457F  // '\x7f', 'E', 'L', 'F' (리틀 엔디안)
#define ELFCLASS32      1           // 32비트 아키텍처
#define ELFDATA2LSB     1           // 리틀 엔디안 (2's complement, little endian)
#define ET_EXEC         2           // 실행 파일
#define EM_386          3           // x86 (i386) 머신
#define PT_LOAD         1           // 로드해야 할 세그먼트


// --- ELF 32비트 기본 자료형 ---
typedef uint32_t Elf32_Addr;
typedef uint32_t Elf32_Off;
typedef uint16_t Elf32_Half;
typedef uint32_t Elf32_Word;
typedef int32_t  Elf32_Sword;

// --- ELF 메인 헤더 구조체 ---
typedef struct {
    unsigned char e_ident[16]; // 매직 넘버 및 아키텍처 정보
    Elf32_Half    e_type;      // 파일 타입 (실행 파일, 오브젝트 등)
    Elf32_Half    e_machine;   // 아키텍처 (x86)
    Elf32_Word    e_version;   // 버전
    Elf32_Addr    e_entry;     // 프로그램 진입점 (Entry Point)
    Elf32_Off     e_phoff;     // 프로그램 헤더 테이블 위치
    Elf32_Off     e_shoff;     // 섹션 헤더 테이블 위치
    Elf32_Word    e_flags;     // 아키텍처 종속 플래그
    Elf32_Half    e_ehsize;    // ELF 헤더 크기
    Elf32_Half    e_phentsize; // 프로그램 헤더 엔트리 하나의 크기
    Elf32_Half    e_phnum;     // 프로그램 헤더 개수
    Elf32_Half    e_shentsize; // 섹션 헤더 엔트리 하나의 크기
    Elf32_Half    e_shnum;     // 섹션 헤더 개수
    Elf32_Half    e_shstrndx;  // 섹션 이름 문자열 테이블 인덱스
} Elf32_Ehdr;

// --- 프로그램 헤더 (세그먼트 헤더) 구조체 ---
typedef struct {
    Elf32_Word    p_type;      // 세그먼트 타입 (PT_LOAD 등)
    Elf32_Off     p_offset;    // 파일 내 세그먼트 위치
    Elf32_Addr    p_vaddr;     // 로드될 가상 주소
    Elf32_Addr    p_paddr;     // 물리 주소 (보통 사용 안함)
    Elf32_Word    p_filesz;    // 파일 내 크기
    Elf32_Word    p_memsz;     // 메모리 내 크기 (BSS 포함)
    Elf32_Word    p_flags;     // 접근 권한 플래그 (R, W, X)
    Elf32_Word    p_align;     // 메모리 정렬 (주로 4KB - 0x1000)
} Elf32_Phdr;

// --- 로더 함수 프로토타입 ---
bool elf_check_supported(Elf32_Ehdr *hdr);
void* elf_load_file(File *file);
int elf_execute_from_path(const char* filepath);

#endif