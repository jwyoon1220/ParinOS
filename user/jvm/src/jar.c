/*
 * user/jvm/src/jar.c — JAR (ZIP) 파일 파서 및 클래스 로더
 *
 * JAR 파일은 ZIP 형식입니다.  이 파일은:
 *   1. ZIP Central Directory를 파싱하여 .class 파일 목록을 구성합니다.
 *   2. classfile_load_from_memory()를 사용해 메모리에서 직접 클래스를 로드합니다.
 *   3. META-INF/MANIFEST.MF 에서 Main-Class를 추출합니다.
 *
 * 지원 형식:
 *   - ZIP 2.0 Local File Header (DEFLATE store 전용 — BTYPE=00)
 *   - DEFLATE 압축 항목은 "not supported" 경고 후 건너뜁니다.
 */

#include "jvm.h"
#include "jar.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "unistd.h"

/* ── ZIP 구조체 ─────────────────────────────────────────────────── */

/* ZIP Local File Header 매직 */
#define ZIP_LFH_SIG  0x04034B50U
/* ZIP Central Directory Entry 매직 */
#define ZIP_CDE_SIG  0x02014B50U
/* ZIP End of Central Directory 매직 */
#define ZIP_EOCD_SIG 0x06054B50U

typedef struct __attribute__((packed)) {
    uint32_t sig;          /* 0x04034B50 */
    uint16_t ver;
    uint16_t flags;
    uint16_t method;       /* 0=stored, 8=deflate */
    uint16_t mod_time;
    uint16_t mod_date;
    uint32_t crc32;
    uint32_t comp_size;
    uint32_t uncomp_size;
    uint16_t fname_len;
    uint16_t extra_len;
    /* filename[fname_len] follows */
    /* extra[extra_len] follows */
    /* data[comp_size] follows */
} zip_lfh_t;

typedef struct __attribute__((packed)) {
    uint32_t sig;          /* 0x06054B50 */
    uint16_t disk_num;
    uint16_t start_disk;
    uint16_t disk_entries;
    uint16_t total_entries;
    uint32_t cd_size;
    uint32_t cd_offset;
    uint16_t comment_len;
} zip_eocd_t;

typedef struct __attribute__((packed)) {
    uint32_t sig;          /* 0x02014B50 */
    uint16_t ver_made;
    uint16_t ver_needed;
    uint16_t flags;
    uint16_t method;
    uint16_t mod_time;
    uint16_t mod_date;
    uint32_t crc32;
    uint32_t comp_size;
    uint32_t uncomp_size;
    uint16_t fname_len;
    uint16_t extra_len;
    uint16_t comment_len;
    uint16_t disk_start;
    uint16_t int_attr;
    uint32_t ext_attr;
    uint32_t lfh_offset;   /* Local File Header 오프셋 */
    /* filename[fname_len] follows */
} zip_cde_t;

/* ── 작은 바이트 읽기 유틸 ──────────────────────────────────────── */
static uint16_t u16le(const uint8_t *p) { return (uint16_t)(p[0] | (p[1]<<8)); }
static uint32_t u32le(const uint8_t *p) { return (uint32_t)(p[0]|(p[1]<<8)|(p[2]<<16)|(p[3]<<24)); }

/* ── JAR 상태 ────────────────────────────────────────────────────── */
static uint8_t *g_jar_data = NULL;
static uint32_t g_jar_size = 0;

/* ── jar_open: 파일 전체를 메모리에 읽기 ────────────────────────── */
int jar_open(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    fseek(f, 0, 2);
    long sz = ftell(f);
    fseek(f, 0, 0);
    if (sz <= 0 || sz > 32 * 1024 * 1024) { fclose(f); return -1; }
    g_jar_data = (uint8_t *)malloc((uint32_t)sz);
    if (!g_jar_data) { fclose(f); return -1; }
    fread(g_jar_data, 1, (uint32_t)sz, f);
    fclose(f);
    g_jar_size = (uint32_t)sz;
    return 0;
}

void jar_close(void) {
    free(g_jar_data);
    g_jar_data = NULL;
    g_jar_size = 0;
}

/* ── EOCD 탐색 ─────────────────────────────────────────────────── */
static const zip_eocd_t *find_eocd(void) {
    if (!g_jar_data || g_jar_size < 22) return NULL;
    /* 끝에서부터 스캔 (최대 65535 bytes 코멘트) */
    int32_t start = (int32_t)g_jar_size - 22;
    int32_t end   = start - 65535;
    if (end < 0) end = 0;
    for (int32_t i = start; i >= end; i--) {
        if (u32le(g_jar_data + i) == ZIP_EOCD_SIG)
            return (const zip_eocd_t *)(g_jar_data + i);
    }
    return NULL;
}

/* ── jar_read_main_class: META-INF/MANIFEST.MF 파싱 ───────────── */
int jar_read_main_class(char *out_class, int max_len) {
    const zip_eocd_t *eocd = find_eocd();
    if (!eocd) return -1;

    uint32_t cd_off  = u32le((const uint8_t *)&eocd->cd_offset);
    uint16_t entries = u16le((const uint8_t *)&eocd->total_entries);

    const uint8_t *p = g_jar_data + cd_off;
    for (uint16_t i = 0; i < entries; i++) {
        if (p + sizeof(zip_cde_t) > g_jar_data + g_jar_size) break;
        if (u32le(p) != ZIP_CDE_SIG) break;

        const zip_cde_t *cde = (const zip_cde_t *)p;
        uint16_t flen  = u16le((const uint8_t *)&cde->fname_len);
        uint16_t elen  = u16le((const uint8_t *)&cde->extra_len);
        uint16_t clen  = u16le((const uint8_t *)&cde->comment_len);
        const char *fname = (const char *)(p + sizeof(zip_cde_t));

        /* META-INF/MANIFEST.MF 탐색 */
        if (flen == 20 && strncmp(fname, "META-INF/MANIFEST.MF", 20) == 0) {
            uint32_t lfh_off = u32le((const uint8_t *)&cde->lfh_offset);
            const uint8_t *lfh_p = g_jar_data + lfh_off;
            if (u32le(lfh_p) != ZIP_LFH_SIG) break;
            const zip_lfh_t *lfh = (const zip_lfh_t *)lfh_p;
            uint16_t lfh_flen  = u16le((const uint8_t *)&lfh->fname_len);
            uint16_t lfh_elen  = u16le((const uint8_t *)&lfh->extra_len);
            uint16_t method    = u16le((const uint8_t *)&lfh->method);
            uint32_t comp_size = u32le((const uint8_t *)&lfh->comp_size);

            if (method != 0) {
                printf("[jar] MANIFEST.MF is compressed (method=%d); cannot read\n", method);
                break;
            }

            const char *mf = (const char *)(lfh_p + sizeof(zip_lfh_t)
                                             + lfh_flen + lfh_elen);
            /* 'Main-Class:' 탐색 */
            const char *key = "Main-Class:";
            uint32_t mf_len = comp_size;
            for (uint32_t j = 0; j + 11 < mf_len; j++) {
                if (strncmp(mf + j, key, 11) == 0) {
                    j += 11;
                    while (j < mf_len && (mf[j] == ' ' || mf[j] == '\t')) j++;
                    int k = 0;
                    while (j < mf_len && mf[j] != '\r' && mf[j] != '\n'
                           && k < max_len - 1) {
                        out_class[k++] = mf[j++];
                    }
                    out_class[k] = '\0';
                    /* 점(.) → 슬래시(/) 변환 (내부 클래스 이름) */
                    for (int m = 0; m < k; m++)
                        if (out_class[m] == '.') out_class[m] = '/';
                    return 0;
                }
            }
        }

        p += sizeof(zip_cde_t) + flen + elen + clen;
    }
    return -1;  /* Main-Class 없음 */
}

/* ── jar_load_class: ZIP에서 클래스 데이터 추출 후 파싱 ─────────── */
class_info_t *jar_load_class(jvm_t *jvm, const char *class_name) {
    const zip_eocd_t *eocd = find_eocd();
    if (!eocd) return NULL;

    uint32_t cd_off  = u32le((const uint8_t *)&eocd->cd_offset);
    uint16_t entries = u16le((const uint8_t *)&eocd->total_entries);

    /* 찾을 파일명: "com/example/Foo.class" */
    char target[256];
    int tlen = (int)strlen(class_name);
    if (tlen + 6 >= 256) return NULL;
    memcpy(target, class_name, (uint32_t)tlen);
    memcpy(target + tlen, ".class", 7);
    tlen += 6;

    const uint8_t *p = g_jar_data + cd_off;
    for (uint16_t i = 0; i < entries; i++) {
        if (p + sizeof(zip_cde_t) > g_jar_data + g_jar_size) break;
        if (u32le(p) != ZIP_CDE_SIG) break;

        const zip_cde_t *cde = (const zip_cde_t *)p;
        uint16_t flen  = u16le((const uint8_t *)&cde->fname_len);
        uint16_t elen  = u16le((const uint8_t *)&cde->extra_len);
        uint16_t clen  = u16le((const uint8_t *)&cde->comment_len);
        const char *fname = (const char *)(p + sizeof(zip_cde_t));

        if (flen == (uint16_t)tlen && strncmp(fname, target, (uint32_t)flen) == 0) {
            uint32_t lfh_off = u32le((const uint8_t *)&cde->lfh_offset);
            const uint8_t *lfh_p = g_jar_data + lfh_off;
            if (u32le(lfh_p) != ZIP_LFH_SIG) return NULL;

            const zip_lfh_t *lfh = (const zip_lfh_t *)lfh_p;
            uint16_t lfh_flen    = u16le((const uint8_t *)&lfh->fname_len);
            uint16_t lfh_elen    = u16le((const uint8_t *)&lfh->extra_len);
            uint16_t method      = u16le((const uint8_t *)&lfh->method);
            uint32_t comp_size   = u32le((const uint8_t *)&lfh->comp_size);
            uint32_t uncomp_size = u32le((const uint8_t *)&lfh->uncomp_size);

            const uint8_t *data = lfh_p + sizeof(zip_lfh_t) + lfh_flen + lfh_elen;

            if (method == 0) {
                /* Stored — parse directly from JAR buffer */
                return classfile_load_from_memory(jvm, data, comp_size);
            } else {
                /* Deflate — not yet supported */
                printf("[jar] class %s is DEFLATE compressed (method=%d); unsupported\n",
                       class_name, method);
                (void)uncomp_size;
                return NULL;
            }
        }

        p += sizeof(zip_cde_t) + flen + elen + clen;
    }
    return NULL;
}

/* ── jar_list: 모든 .class 항목 열거 ───────────────────────────── */
void jar_list(void) {
    const zip_eocd_t *eocd = find_eocd();
    if (!eocd) { printf("[jar] invalid ZIP\n"); return; }

    uint32_t cd_off  = u32le((const uint8_t *)&eocd->cd_offset);
    uint16_t entries = u16le((const uint8_t *)&eocd->total_entries);

    const uint8_t *p = g_jar_data + cd_off;
    for (uint16_t i = 0; i < entries; i++) {
        if (u32le(p) != ZIP_CDE_SIG) break;
        const zip_cde_t *cde = (const zip_cde_t *)p;
        uint16_t flen = u16le((const uint8_t *)&cde->fname_len);
        uint16_t elen = u16le((const uint8_t *)&cde->extra_len);
        uint16_t clen = u16le((const uint8_t *)&cde->comment_len);
        const char *fname = (const char *)(p + sizeof(zip_cde_t));

        /* .class 항목만 출력 */
        if (flen > 6 && strncmp(fname + flen - 6, ".class", 6) == 0) {
            printf("  ");
            for (uint16_t j = 0; j < flen; j++) putchar(fname[j]);
            printf("\n");
        }
        p += sizeof(zip_cde_t) + flen + elen + clen;
    }
}
