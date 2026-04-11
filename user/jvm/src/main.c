/*
 * main.c — ParinOS Tiny JVM 엔트리포인트
 *
 * 사용법:
 *   jvm <classname>   [args...]
 *   jvm /path/Hello   [args...]
 *   jvm Hello.class   [args...]
 *
 * classpath 기본값: 클래스 파일과 같은 디렉터리, 없으면 /classes
 */

#include "jvm.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"

/* ── 전역 JVM 인스턴스 ───────────────────────────────────────────────── */
jvm_t g_jvm;

int main(int argc, const char **argv) {
    if (argc < 2) {
        printf("Usage: jvm <classname> [args...]\n");
        printf("  e.g. jvm /classes/Hello\n");
        return 1;
    }

    /* ── JVM 초기화 ──────────────────────────────────────────────── */
    memset(&g_jvm, 0, sizeof(jvm_t));
    heap_init(&g_jvm,
              1u * 1024u * 1024u,   /* 힙 1MB  */
              64u * 1024u);          /* JIT 코드 캐시 64KB */

    /* System.out PrintStream 싱글턴 */
    g_jvm.system_out = (jobj_t *)heap_alloc(&g_jvm, sizeof(jobj_t));
    if (g_jvm.system_out) g_jvm.system_out->type = OBJ_PRINTSTREAM;

    /* ── 경로 분석 ────────────────────────────────────────────────── */
    const char *arg = argv[1];
    char classpath[256];
    char classname[128];  /* 내부 이름 (예: "Hello", "com/example/Foo") */
    char classfile[512];  /* .class 파일 전체 경로 */

    /* 슬래시가 있으면 디렉터리 추출 */
    const char *last_slash = strrchr(arg, '/');
    if (last_slash) {
        int dirlen = (int)(last_slash - arg);
        if (dirlen >= (int)sizeof(classpath)) dirlen = (int)sizeof(classpath) - 1;
        memcpy(classpath, arg, (size_t)dirlen);
        classpath[dirlen] = '\0';
        strncpy(classname, last_slash + 1, sizeof(classname) - 1);
        classname[sizeof(classname) - 1] = '\0';
    } else {
        strncpy(classpath, "/classes", sizeof(classpath) - 1);
        classpath[sizeof(classpath) - 1] = '\0';
        strncpy(classname, arg, sizeof(classname) - 1);
        classname[sizeof(classname) - 1] = '\0';
    }

    /* ".class" 확장자 제거 */
    int nl = (int)strlen(classname);
    if (nl > 6 && strcmp(classname + nl - 6, ".class") == 0)
        classname[nl - 6] = '\0';

    strncpy(g_jvm.loader.classpath, classpath, sizeof(g_jvm.loader.classpath) - 1);

    snprintf(classfile, sizeof(classfile), "%s/%s.class", classpath, classname);

    /* ── 클래스 로드 ─────────────────────────────────────────────── */
    class_info_t *klass = classfile_load(&g_jvm, classfile);
    if (!klass) {
        printf("[jvm] failed to load: %s\n", classfile);
        return 1;
    }
    printf("[jvm] loaded %s\n", klass->name ? klass->name : classname);

    /* ── main 메서드 탐색 ────────────────────────────────────────── */
    method_info_t *main_m =
        class_find_method(klass, "main", "([Ljava/lang/String;)V");
    if (!main_m)
        main_m = class_find_method(klass, "main", (char *)0);
    if (!main_m || !main_m->code) {
        printf("[jvm] main() not found in %s\n",
               klass->name ? klass->name : classname);
        return 1;
    }

    /* ── String[] args 구성 ──────────────────────────────────────── */
    int napp_args = argc - 2;   /* argv[2..] */
    jobj_t *str_arr = obj_array_ref(&g_jvm, napp_args);
    for (int i = 0; i < napp_args; i++) {
        int slen = (int)strlen(argv[i + 2]);
        jobj_t *s = obj_string(&g_jvm, argv[i + 2], slen);
        if (str_arr && str_arr->rarr.data)
            str_arr->rarr.data[i] = s;
    }

    /* ── main 실행 ───────────────────────────────────────────────── */
    jval_t main_args[1];
    main_args[0] = JVAL_REF(str_arr);
    interp_exec(&g_jvm, klass, main_m, main_args, 1);

    return 0;
}
