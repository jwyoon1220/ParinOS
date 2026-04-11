/*
 * classfile.c — Java .class 파일 파서
 *
 * 지원: Java 8 이하 바이트코드 (major version 52 이하).
 *       module-info.class 불필요.
 */

#include "jvm.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "unistd.h"

/* ── 빅엔디언 읽기 헬퍼 ─────────────────────────────────────────────── */
static uint16_t r16(const uint8_t *p) {
    return (uint16_t)(((unsigned)p[0] << 8) | p[1]);
}
static uint32_t r32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
}

/* ── cp_utf8: 상수 풀에서 UTF-8 문자열 조회 ────────────────────────── */
const char *cp_utf8(class_info_t *klass, int idx) {
    if (idx <= 0 || idx >= klass->cp_count) return "";
    if (klass->cp[idx].tag != CP_UTF8)      return "";
    return klass->cp[idx].utf8 ? klass->cp[idx].utf8 : "";
}

/* ── 클래스 내 메서드 검색 ──────────────────────────────────────────── */
method_info_t *class_find_method(class_info_t *klass,
                                  const char *name, const char *desc) {
    for (int i = 0; i < klass->method_count; i++) {
        if (!klass->methods[i].name) continue;
        if (strcmp(klass->methods[i].name, name) != 0) continue;
        if (desc && klass->methods[i].descriptor &&
            strcmp(klass->methods[i].descriptor, desc) != 0) continue;
        return &klass->methods[i];
    }
    return (method_info_t *)0;
}

/* ── 클래스 내 필드 검색 ───────────────────────────────────────────── */
field_info_t *class_find_field(class_info_t *klass, const char *name) {
    for (int i = 0; i < klass->field_count; i++) {
        if (klass->fields[i].name &&
            strcmp(klass->fields[i].name, name) == 0)
            return &klass->fields[i];
    }
    return (field_info_t *)0;
}

/* ── 로더 내 클래스 검색 ───────────────────────────────────────────── */
static class_info_t *classloader_find(jvm_t *jvm, const char *name) {
    for (int i = 0; i < jvm->loader.count; i++) {
        if (jvm->loader.classes[i]->name &&
            strcmp(jvm->loader.classes[i]->name, name) == 0)
            return jvm->loader.classes[i];
    }
    return (class_info_t *)0;
}

/* ── classfile_load: 디스크에서 .class 파일 로드 및 파싱 ──────────── */
class_info_t *classfile_load(jvm_t *jvm, const char *path) {
    int fd = open(path, O_RDONLY, 0);
    if (fd < 0) {
        printf("[jvm] open failed: %s\n", path);
        return (class_info_t *)0;
    }
    int size = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    if (size <= 0 || size > 65536) {
        printf("[jvm] invalid class file size: %s (%d)\n", path, size);
        close(fd);
        return (class_info_t *)0;
    }

    uint8_t *buf = (uint8_t *)malloc((size_t)size);
    if (!buf) { close(fd); return (class_info_t *)0; }

    int n = read(fd, buf, size);
    close(fd);
    if (n != size) { free(buf); return (class_info_t *)0; }

    /* 매직 넘버 확인 */
    if (r32(buf) != 0xCAFEBABE) {
        printf("[jvm] not a .class file (bad magic): %s\n", path);
        free(buf);
        return (class_info_t *)0;
    }

    class_info_t *klass = (class_info_t *)malloc(sizeof(class_info_t));
    if (!klass) { free(buf); return (class_info_t *)0; }
    memset(klass, 0, sizeof(class_info_t));
    klass->raw = buf;

    const uint8_t *p = buf + 8;   /* magic(4) + minor(2) + major(2) 건너뜀 */

    /* ── 상수 풀 ──────────────────────────────────────────────────── */
    int cp_count = (int)r16(p); p += 2;
    if (cp_count >= MAX_CP) cp_count = MAX_CP;
    klass->cp_count = cp_count;

    for (int i = 1; i < cp_count; i++) {
        uint8_t tag = *p++;
        klass->cp[i].tag = tag;
        switch (tag) {
        case CP_UTF8: {
            int len = (int)r16(p); p += 2;
            char *s = (char *)malloc((size_t)len + 1);
            if (s) { memcpy(s, p, (size_t)len); s[len] = '\0'; }
            klass->cp[i].utf8 = s;
            p += len;
            break;
        }
        case CP_INTEGER:
            klass->cp[i].ival = (int32_t)r32(p); p += 4;
            break;
        case 4:  p += 4; break;   /* FLOAT  */
        case 5:  p += 8; i++; break; /* LONG   (2슬롯) */
        case 6:  p += 8; i++; break; /* DOUBLE (2슬롯) */
        case CP_CLASS:
            klass->cp[i].class_idx = r16(p); p += 2;
            break;
        case CP_STRING:
            klass->cp[i].string_idx = r16(p); p += 2;
            break;
        case CP_FIELDREF:
        case CP_METHODREF:
        case CP_IFACEREF:
            klass->cp[i].ref.cls = r16(p);
            klass->cp[i].ref.nat = r16(p + 2);
            p += 4;
            break;
        case CP_NAMETYPE:
            klass->cp[i].nat.name = r16(p);
            klass->cp[i].nat.desc = r16(p + 2);
            p += 4;
            break;
        case 15: p += 3; break;  /* MethodHandle  */
        case 16: p += 2; break;  /* MethodType    */
        case 17: p += 4; break;  /* Dynamic       */
        case 18: p += 4; break;  /* InvokeDynamic */
        case 19: p += 2; break;  /* Module        */
        case 20: p += 2; break;  /* Package       */
        default:
            printf("[jvm] unknown CP tag %d at index %d\n", tag, i);
            goto parse_error;
        }
    }

    /* access_flags, this_class, super_class */
    p += 2;   /* access_flags */
    int this_idx = (int)r16(p); p += 2;
    p += 2;   /* super_class */

    /* 클래스 이름 설정 */
    const char *cname = (this_idx > 0 && this_idx < cp_count &&
                         klass->cp[this_idx].tag == CP_CLASS)
                      ? cp_utf8(klass, klass->cp[this_idx].class_idx)
                      : path;
    klass->name = (char *)malloc(strlen(cname) + 1);
    if (klass->name) strcpy(klass->name, cname);

    /* 인터페이스 건너뜀 */
    int icount = (int)r16(p); p += 2;
    p += (size_t)icount * 2;

    /* ── 필드 ────────────────────────────────────────────────────── */
    int fcount = (int)r16(p); p += 2;
    if (fcount > MAX_FIELDS) fcount = MAX_FIELDS;
    klass->field_count = fcount;
    for (int i = 0; i < fcount; i++) {
        klass->fields[i].access_flags = r16(p); p += 2;
        klass->fields[i].name        = (char *)cp_utf8(klass, (int)r16(p)); p += 2;
        klass->fields[i].descriptor  = (char *)cp_utf8(klass, (int)r16(p)); p += 2;
        klass->fields[i].static_val  = JVAL_INT(0);
        int ac = (int)r16(p); p += 2;
        for (int j = 0; j < ac; j++) {
            p += 2;
            uint32_t alen = r32(p); p += 4;
            p += alen;
        }
    }

    /* ── 메서드 ──────────────────────────────────────────────────── */
    int mcount = (int)r16(p); p += 2;
    if (mcount > MAX_METHODS) mcount = MAX_METHODS;
    klass->method_count = mcount;
    for (int i = 0; i < mcount; i++) {
        klass->methods[i].access_flags = r16(p); p += 2;
        klass->methods[i].name        = (char *)cp_utf8(klass, (int)r16(p)); p += 2;
        klass->methods[i].descriptor  = (char *)cp_utf8(klass, (int)r16(p)); p += 2;
        klass->methods[i].code        = (uint8_t *)0;
        klass->methods[i].code_len    = 0;
        klass->methods[i].call_count  = 0;
        klass->methods[i].jit_code    = (void *)0;
        int ac = (int)r16(p); p += 2;
        for (int j = 0; j < ac; j++) {
            int anidx      = (int)r16(p); p += 2;
            uint32_t alen  = r32(p);      p += 4;
            const char *an = cp_utf8(klass, anidx);
            if (strcmp(an, "Code") == 0) {
                klass->methods[i].max_stack  = r16(p);
                klass->methods[i].max_locals = r16(p + 2);
                uint32_t clen = r32(p + 4);
                /* code 포인터는 raw 버퍼 안을 가리킴 */
                klass->methods[i].code     = (uint8_t *)(p + 8);
                klass->methods[i].code_len = (uint16_t)clen;
            }
            p += alen;
        }
    }

    /* 클래스 로더에 등록 */
    if (jvm->loader.count < MAX_CLASSES)
        jvm->loader.classes[jvm->loader.count++] = klass;

    return klass;

parse_error:
    free(buf);
    free(klass);
    return (class_info_t *)0;
}

/* ── classloader_resolve: 이름으로 클래스 검색 또는 로드 ─────────────── */
class_info_t *classloader_resolve(jvm_t *jvm, const char *name) {
    /* 이미 로드된 클래스인지 확인 */
    class_info_t *klass = classloader_find(jvm, name);
    if (klass) return klass;

    /* java/lang, java/io 등 표준 클래스는 네이티브로 처리하므로 로드 불필요 */
    if (strncmp(name, "java/", 5) == 0 ||
        strncmp(name, "javax/", 6) == 0 ||
        strncmp(name, "sun/", 4) == 0)
        return (class_info_t *)0;

    /* classpath에서 로드 시도 */
    char path[512];
    const char *cp = jvm->loader.classpath[0]
                   ? jvm->loader.classpath : "/classes";
    snprintf(path, sizeof(path), "%s/%s.class", cp, name);
    klass = classfile_load(jvm, path);

    /* static 초기화 메서드 실행 */
    if (klass) {
        method_info_t *clinit = class_find_method(klass, "<clinit>", "()V");
        if (clinit && clinit->code) {
            interp_exec(jvm, klass, clinit, (jval_t *)0, 0);
        }
    }
    return klass;
}
