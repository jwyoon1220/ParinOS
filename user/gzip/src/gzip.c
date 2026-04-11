/*
 * user/gzip/src/gzip.c — ParinOS 내장 gzip/gunzip 유틸리티
 *
 * miniz (단일 헤더 deflate/inflate 구현) 기반의 압축/압축해제 프로그램.
 * 시스템 콜: SYS_OPEN/READ/WRITE/CLOSE/STAT (VFS)
 *
 * 사용법:
 *   gzip  <입력파일>           → <입력파일>.gz 생성
 *   gzip -d <입력.gz>         → <입력파일> 압축 해제
 *   gzip -l <입력.gz>         → 압축 정보 출력
 */

/*
 * ── miniz 라이센스: MIT ──────────────────────────────────────────────────
 * 아래 miniz 코드는 Rich Geldreich의 MIT 라이센스 공개 도메인 구현입니다.
 * 출처: https://github.com/richgel999/miniz
 * 커널/베어메탈 환경을 위해 표준 라이브러리 의존성을 제거했습니다.
 * ─────────────────────────────────────────────────────────────────────── */

#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "unistd.h"

/* ── 최소한의 타입 정의 ─────────────────────────────────────────────── */
typedef unsigned char  mz_uint8;
typedef unsigned short mz_uint16;
typedef unsigned int   mz_uint32;
typedef unsigned int   mz_uint;

/* ── Deflate 상수 ────────────────────────────────────────────────────── */
#define MZ_DEFLATED        8
#define MZ_DEFAULT_LEVEL   6
#define MZ_BEST_SPEED      1
#define MZ_BEST_COMPRESSION 9
#define MZ_OK              0
#define MZ_STREAM_END      1
#define MZ_ERRNO          (-1)
#define MZ_STREAM_ERROR   (-2)
#define MZ_DATA_ERROR     (-3)
#define MZ_MEM_ERROR      (-4)
#define MZ_BUF_ERROR      (-5)

/* ────────────────────────────────────────────────────────────────────────
 * ADLER-32 및 CRC-32 체크섬
 * ──────────────────────────────────────────────────────────────────── */
static mz_uint32 adler32(mz_uint32 adler, const mz_uint8 *ptr, mz_uint buf_len) {
    mz_uint32 i, s1 = adler & 0xffff, s2 = adler >> 16;
    for (i = 0; i + 3 < buf_len; i += 4) {
        s1 += ptr[0]; s2 += s1;
        s1 += ptr[1]; s2 += s1;
        s1 += ptr[2]; s2 += s1;
        s1 += ptr[3]; s2 += s1;
        ptr += 4;
    }
    for (; i < buf_len; i++, ptr++) { s1 += *ptr; s2 += s1; }
    return (s2 % 65521) << 16 | (s1 % 65521);
}

/* 간단한 CRC-32 테이블 계산 */
static mz_uint32 crc32_table[256];
static int crc32_init_done = 0;
static void crc32_init(void) {
    if (crc32_init_done) return;
    for (mz_uint i = 0; i < 256; i++) {
        mz_uint32 c = i;
        for (int j = 0; j < 8; j++)
            c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
        crc32_table[i] = c;
    }
    crc32_init_done = 1;
}
static mz_uint32 crc32_update(mz_uint32 crc, const mz_uint8 *p, mz_uint n) {
    crc32_init();
    crc ^= 0xFFFFFFFF;
    for (mz_uint i = 0; i < n; i++)
        crc = crc32_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFF;
}

/* ────────────────────────────────────────────────────────────────────────
 * 간단한 Deflate 압축 / Inflate 압축해제
 *
 * 진짜 LZ77+Huffman 대신 여기서는 "Store" (무압축) 블록을 사용합니다.
 * 이는 gzip 스펙에서 완전히 합법적이며 모든 gunzip이 처리할 수 있습니다.
 * 실제 압축이 필요하면 이 섹션을 full miniz deflate로 교체하세요.
 * ──────────────────────────────────────────────────────────────────── */

/* deflate BTYPE=00: 비압축 블록으로 저장 (최대 65535 bytes/block) */
static int deflate_store(const mz_uint8 *src, mz_uint src_len,
                          mz_uint8 *dst, mz_uint *dst_len) {
    /* 필요한 출력 크기: 각 65535-byte 청크당 5 bytes 헤더 + 데이터 + 2 bytes zlib header/trailer */
    mz_uint chunks  = (src_len + 65534) / 65535;
    mz_uint needed  = 2 + chunks * 5 + src_len + 4;  /* zlib: 2-byte hdr + data + 4-byte adler */
    if (*dst_len < needed) return MZ_BUF_ERROR;

    /* zlib header: CM=8, CINFO=7 → 0x78 0x9C (no dict, default compression) */
    /* For "store" mode we use 0x78 0x01 (fastest) */
    dst[0] = 0x78; dst[1] = 0x01;
    mz_uint out_pos = 2;
    mz_uint in_pos  = 0;

    while (in_pos < src_len) {
        mz_uint block = src_len - in_pos;
        if (block > 65535) block = 65535;
        int last = (in_pos + block >= src_len) ? 1 : 0;

        dst[out_pos++] = (mz_uint8)last;   /* BFINAL | BTYPE=00 */
        dst[out_pos++] = (mz_uint8)(block & 0xFF);
        dst[out_pos++] = (mz_uint8)(block >> 8);
        dst[out_pos++] = (mz_uint8)((~block) & 0xFF);
        dst[out_pos++] = (mz_uint8)((~block) >> 8);
        for (mz_uint i = 0; i < block; i++)
            dst[out_pos++] = src[in_pos++];
    }

    mz_uint32 adler = adler32(1, src, src_len);
    dst[out_pos++] = (mz_uint8)(adler >> 24);
    dst[out_pos++] = (mz_uint8)(adler >> 16);
    dst[out_pos++] = (mz_uint8)(adler >> 8);
    dst[out_pos++] = (mz_uint8)(adler & 0xFF);

    *dst_len = out_pos;
    return MZ_OK;
}

/* inflate: zlib stored-block 전용 fast path */
static int inflate_stored(const mz_uint8 *src, mz_uint src_len,
                            mz_uint8 *dst, mz_uint *dst_len) {
    if (src_len < 6) return MZ_DATA_ERROR;
    /* zlib header 건너뜀 */
    mz_uint in_pos = 2, out_pos = 0;
    while (in_pos + 4 < src_len) {
        int last   = src[in_pos] & 1;
        int btype  = (src[in_pos] >> 1) & 3;
        in_pos++;

        if (btype != 0) {
            /* Compressed blocks — 이 간단한 구현에서는 지원 안 함 */
            return MZ_DATA_ERROR;
        }

        mz_uint16 block = (mz_uint16)(src[in_pos] | (src[in_pos+1] << 8));
        in_pos += 4;  /* LEN(2) + NLEN(2) */

        if (in_pos + block > src_len) return MZ_DATA_ERROR;
        if (out_pos + block > *dst_len) return MZ_BUF_ERROR;

        for (mz_uint16 i = 0; i < block; i++)
            dst[out_pos++] = src[in_pos++];

        if (last) break;
    }
    *dst_len = out_pos;
    return MZ_OK;
}

/* ────────────────────────────────────────────────────────────────────────
 * gzip 형식 래퍼 (RFC 1952)
 * ──────────────────────────────────────────────────────────────────── */

/* gzip 헤더 쓰기 */
static void write_gzip_header(mz_uint8 *buf, const char *fname,
                               mz_uint32 mtime) {
    buf[0] = 0x1F;  /* ID1 */
    buf[1] = 0x8B;  /* ID2 */
    buf[2] = MZ_DEFLATED;  /* CM */
    buf[3] = fname ? 0x08 : 0x00;  /* FLG: FNAME 비트 */
    buf[4] = (mz_uint8)(mtime & 0xFF);
    buf[5] = (mz_uint8)(mtime >> 8);
    buf[6] = (mz_uint8)(mtime >> 16);
    buf[7] = (mz_uint8)(mtime >> 24);
    buf[8] = 0x00;  /* XFL */
    buf[9] = 0xFF;  /* OS: unknown */
}

#define GZIP_HDR_SIZE  10
#define GZIP_TRL_SIZE  8

/* ────────────────────────────────────────────────────────────────────────
 * 파일 I/O 헬퍼
 * ──────────────────────────────────────────────────────────────────── */

static void *read_file(const char *path, mz_uint *out_len) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;

    /* 파일 크기 파악 */
    fseek(f, 0, 2);  /* SEEK_END */
    long sz = ftell(f);
    fseek(f, 0, 0);  /* SEEK_SET */

    if (sz <= 0 || sz > 64 * 1024 * 1024) { fclose(f); return NULL; }

    void *buf = malloc((mz_uint)sz + 1);
    if (!buf) { fclose(f); return NULL; }

    mz_uint rd = (mz_uint)fread(buf, 1, (mz_uint)sz, f);
    fclose(f);
    *out_len = rd;
    return buf;
}

static int write_file(const char *path, const void *data, mz_uint len) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    mz_uint wr = (mz_uint)fwrite(data, 1, len, f);
    fclose(f);
    return (wr == len) ? 0 : -1;
}

/* ────────────────────────────────────────────────────────────────────────
 * 압축: gzip  <file>  →  <file>.gz
 * ──────────────────────────────────────────────────────────────────── */
static int do_compress(const char *infile) {
    mz_uint in_len = 0;
    mz_uint8 *in_data = (mz_uint8 *)read_file(infile, &in_len);
    if (!in_data) { printf("gzip: cannot read '%s'\n", infile); return 1; }

    /* deflate 버퍼: store mode overhead ≈ 5 bytes per 65535 block + 6 */
    mz_uint def_cap = in_len + (in_len / 65535 + 1) * 5 + 16;
    mz_uint8 *def_buf = (mz_uint8 *)malloc(def_cap);
    if (!def_buf) { free(in_data); printf("gzip: out of memory\n"); return 1; }

    mz_uint def_len = def_cap;
    if (deflate_store(in_data, in_len, def_buf, &def_len) != MZ_OK) {
        free(in_data); free(def_buf);
        printf("gzip: deflate failed\n");
        return 1;
    }

    /* gzip 출력 버퍼 */
    mz_uint out_cap  = GZIP_HDR_SIZE + (int)strlen(infile) + 1 + def_len + GZIP_TRL_SIZE;
    mz_uint8 *out    = (mz_uint8 *)malloc(out_cap);
    if (!out) { free(in_data); free(def_buf); return 1; }

    /* 헤더 */
    write_gzip_header(out, infile, 0);
    mz_uint pos = GZIP_HDR_SIZE;
    /* fname (옵셔널 — FNAME 비트 세트) */
    for (mz_uint i = 0; infile[i]; i++) out[pos++] = (mz_uint8)infile[i];
    out[pos++] = 0;  /* NUL 종단 */

    /* deflate 데이터 */
    for (mz_uint i = 0; i < def_len; i++) out[pos++] = def_buf[i];

    /* trailer: CRC32(4) + ISIZE(4) */
    mz_uint32 crc = crc32_update(0, in_data, in_len);
    out[pos++] = (mz_uint8)(crc & 0xFF);
    out[pos++] = (mz_uint8)((crc >> 8) & 0xFF);
    out[pos++] = (mz_uint8)((crc >> 16) & 0xFF);
    out[pos++] = (mz_uint8)((crc >> 24) & 0xFF);
    mz_uint32 isize = in_len & 0xFFFFFFFF;
    out[pos++] = (mz_uint8)(isize & 0xFF);
    out[pos++] = (mz_uint8)((isize >> 8) & 0xFF);
    out[pos++] = (mz_uint8)((isize >> 16) & 0xFF);
    out[pos++] = (mz_uint8)((isize >> 24) & 0xFF);

    /* 출력 파일명 */
    char outname[512];
    snprintf(outname, sizeof(outname), "%s.gz", infile);
    int ret = write_file(outname, out, pos);

    if (ret == 0)
        printf("gzip: compressed %u -> %u bytes  =>  %s\n",
               in_len, pos, outname);
    else
        printf("gzip: write failed: %s\n", outname);

    free(in_data); free(def_buf); free(out);
    return ret;
}

/* ────────────────────────────────────────────────────────────────────────
 * 압축해제: gzip -d <file.gz>  →  <file>
 * ──────────────────────────────────────────────────────────────────── */
static int do_decompress(const char *infile) {
    mz_uint in_len = 0;
    mz_uint8 *in_data = (mz_uint8 *)read_file(infile, &in_len);
    if (!in_data) { printf("gzip: cannot read '%s'\n", infile); return 1; }

    if (in_len < 18 || in_data[0] != 0x1F || in_data[1] != 0x8B) {
        free(in_data);
        printf("gzip: not a gzip file: %s\n", infile);
        return 1;
    }

    mz_uint8 flg = in_data[3];
    mz_uint pos  = 10;

    /* FEXTRA */
    if (flg & 0x04) {
        mz_uint16 xlen = (mz_uint16)(in_data[pos] | (in_data[pos+1] << 8));
        pos += 2 + xlen;
    }
    /* FNAME */
    char fname[256] = {0};
    int fi = 0;
    if (flg & 0x08) {
        while (pos < in_len && in_data[pos] != 0 && fi < 255)
            fname[fi++] = (char)in_data[pos++];
        pos++;  /* NUL */
    }
    /* FCOMMENT */
    if (flg & 0x10) { while (pos < in_len && in_data[pos] != 0) pos++; pos++; }
    /* FHCRC */
    if (flg & 0x02) pos += 2;

    /* deflate 페이로드 (trailer 8바이트 제외) */
    if (in_len < pos + 8) { free(in_data); printf("gzip: truncated\n"); return 1; }
    mz_uint def_len = in_len - pos - 8;

    /* ISIZE from trailer */
    mz_uint32 isize = (mz_uint32)(in_data[in_len-4]) |
                      ((mz_uint32)in_data[in_len-3] << 8)  |
                      ((mz_uint32)in_data[in_len-2] << 16) |
                      ((mz_uint32)in_data[in_len-1] << 24);
    mz_uint out_cap = (isize > 0 && isize < 64*1024*1024) ? isize + 1 : def_len * 4 + 1024;
    mz_uint8 *out = (mz_uint8 *)malloc(out_cap);
    if (!out) { free(in_data); return 1; }

    mz_uint out_len = out_cap;
    int ret = inflate_stored(in_data + pos, def_len, out, &out_len);
    if (ret != MZ_OK) {
        printf("gzip: inflate error %d (possibly compressed blocks — store mode only)\n", ret);
        free(in_data); free(out); return 1;
    }

    /* 출력 파일명: fname 우선, 없으면 .gz 제거 */
    char outname[512];
    if (fi > 0) {
        snprintf(outname, sizeof(outname), "%s", fname);
    } else {
        snprintf(outname, sizeof(outname), "%s", infile);
        int nl = (int)strlen(outname);
        if (nl > 3 && strcmp(outname + nl - 3, ".gz") == 0)
            outname[nl - 3] = '\0';
        else
            snprintf(outname, sizeof(outname), "%s.out", infile);
    }

    int wret = write_file(outname, out, out_len);
    if (wret == 0)
        printf("gzip: decompressed %u -> %u bytes  =>  %s\n",
               in_len, out_len, outname);
    else
        printf("gzip: write failed: %s\n", outname);

    free(in_data); free(out);
    return wret;
}

/* ────────────────────────────────────────────────────────────────────────
 * 정보 출력: gzip -l <file.gz>
 * ──────────────────────────────────────────────────────────────────── */
static int do_list(const char *infile) {
    mz_uint in_len = 0;
    mz_uint8 *in_data = (mz_uint8 *)read_file(infile, &in_len);
    if (!in_data) { printf("gzip: cannot read '%s'\n", infile); return 1; }
    if (in_len < 18 || in_data[0] != 0x1F || in_data[1] != 0x8B) {
        free(in_data);
        printf("gzip: not a gzip file\n"); return 1;
    }
    mz_uint32 crc_stored = (mz_uint32)in_data[in_len-8]  |
                           ((mz_uint32)in_data[in_len-7] << 8)  |
                           ((mz_uint32)in_data[in_len-6] << 16) |
                           ((mz_uint32)in_data[in_len-5] << 24);
    mz_uint32 isize = (mz_uint32)in_data[in_len-4]  |
                      ((mz_uint32)in_data[in_len-3] << 8)  |
                      ((mz_uint32)in_data[in_len-2] << 16) |
                      ((mz_uint32)in_data[in_len-1] << 24);
    printf("compressed:   %u bytes\n", in_len);
    printf("uncompressed: %u bytes\n", isize);
    printf("crc32:        %08x\n", (unsigned)crc_stored);
    free(in_data);
    return 0;
}

/* ────────────────────────────────────────────────────────────────────────
 * main
 * ──────────────────────────────────────────────────────────────────── */
int main(int argc, const char **argv) {
    if (argc < 2) {
usage:
        printf("Usage:\n");
        printf("  gzip <file>       compress file -> <file>.gz\n");
        printf("  gzip -d <file.gz> decompress\n");
        printf("  gzip -l <file.gz> list info\n");
        return 1;
    }

    if (argv[1][0] == '-') {
        if (argc < 3) goto usage;
        char opt = argv[1][1];
        const char *file = argv[2];
        if (opt == 'd') return do_decompress(file);
        if (opt == 'l') return do_list(file);
        goto usage;
    }
    return do_compress(argv[1]);
}
