/*
 * main.c — ParinOS Tiny JVM 엔트리포인트
 *
 * 사용법:
 *   jvm <classname>   [args...]  → .class 파일 실행
 *   jvm /path/Hello   [args...]
 *   jvm Hello.class   [args...]
 *   jvm myapp.jar     [args...]  → JAR 실행 (Main-Class 자동 탐색)
 *   jvm -jar myapp.jar [args...] → JAR 실행 (명시적)
 *   jvm -jl myapp.jar            → JAR 내 .class 목록 출력
 *
 * classpath 기본값: 클래스 파일과 같은 디렉터리, 없으면 /classes
 */

#include "jvm.h"
#include "jar.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"

/* ── 전역 JVM 인스턴스 ───────────────────────────────────────────────── */
jvm_t g_jvm;

/* ── JAR 모드 실행 ──────────────────────────────────────────────────── */
static int run_jar(const char *jar_path, int argc, const char **argv) {
    if (jar_open(jar_path) < 0) {
        printf("[jvm] cannot open JAR: %s\n", jar_path);
        return 1;
    }

    char main_class[256];
    if (jar_read_main_class(main_class, sizeof(main_class)) < 0) {
        printf("[jvm] no Main-Class in MANIFEST.MF — use: jvm -jl %s\n", jar_path);
        jar_close();
        return 1;
    }
    printf("[jvm] JAR main class: %s\n", main_class);

    memset(&g_jvm, 0, sizeof(jvm_t));
    heap_init(&g_jvm, 1u * 1024u * 1024u, 64u * 1024u);
    g_jvm.system_out = (jobj_t *)heap_alloc(&g_jvm, sizeof(jobj_t));
    if (g_jvm.system_out) g_jvm.system_out->type = OBJ_PRINTSTREAM;
    /* classpath 빈 문자열 → jar_load_class 가 loader를 오버라이드 */
    g_jvm.loader.classpath[0] = '\0';

    class_info_t *klass = jar_load_class(&g_jvm, main_class);
    if (!klass) {
        printf("[jvm] cannot load class '%s' from JAR\n", main_class);
        jar_close();
        return 1;
    }

    method_info_t *main_m = class_find_method(klass, "main", "([Ljava/lang/String;)V");
    if (!main_m) main_m = class_find_method(klass, "main", (char *)0);
    if (!main_m || !main_m->code) {
        printf("[jvm] main() not found\n");
        jar_close();
        return 1;
    }

    int napp = argc;
    jobj_t *str_arr = obj_array_ref(&g_jvm, napp);
    for (int i = 0; i < napp; i++) {
        int slen = (int)strlen(argv[i]);
        jobj_t *s = obj_string(&g_jvm, argv[i], slen);
        if (str_arr && str_arr->rarr.data) str_arr->rarr.data[i] = s;
    }
    jval_t main_args[1];
    main_args[0] = JVAL_REF(str_arr);
    interp_exec(&g_jvm, klass, main_m, main_args, 1);

    jar_close();
    return 0;
}

int main(int argc, const char **argv) {
    if (argc < 2) {
        printf("Usage:\n");
        printf("  jvm <classname|file.class> [args...]\n");
        printf("  jvm file.jar               [args...]\n");
        printf("  jvm -jar file.jar          [args...]\n");
        printf("  jvm -jl  file.jar          (list classes)\n");
        return 1;
    }

    /* ── -jar: 명시적 JAR 실행 ──────────────────────────────────── */
    if (strcmp(argv[1], "-jar") == 0) {
        if (argc < 3) { printf("jvm: -jar requires a JAR file\n"); return 1; }
        return run_jar(argv[2], argc - 3, argv + 3);
    }

    /* ── -jl: JAR 내 클래스 목록 ────────────────────────────────── */
    if (strcmp(argv[1], "-jl") == 0) {
        if (argc < 3) { printf("jvm: -jl requires a JAR file\n"); return 1; }
        if (jar_open(argv[2]) < 0) {
            printf("jvm: cannot open JAR: %s\n", argv[2]); return 1;
        }
        jar_list();
        jar_close();
        return 0;
    }

    /* ── .jar 확장자 자동 감지 ──────────────────────────────────── */
    {
        int al = (int)strlen(argv[1]);
        if (al > 4 && strcmp(argv[1] + al - 4, ".jar") == 0)
            return run_jar(argv[1], argc - 2, argv + 2);
    }

    /* ── 일반 .class 파일 실행 ──────────────────────────────────── */
    memset(&g_jvm, 0, sizeof(jvm_t));
    heap_init(&g_jvm, 1u * 1024u * 1024u, 64u * 1024u);
    g_jvm.system_out = (jobj_t *)heap_alloc(&g_jvm, sizeof(jobj_t));
    if (g_jvm.system_out) g_jvm.system_out->type = OBJ_PRINTSTREAM;

    const char *arg = argv[1];
    char classpath[256];
    char classname[128];
    char classfile[512];

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

    int nl = (int)strlen(classname);
    if (nl > 6 && strcmp(classname + nl - 6, ".class") == 0)
        classname[nl - 6] = '\0';

    strncpy(g_jvm.loader.classpath, classpath, sizeof(g_jvm.loader.classpath) - 1);
    snprintf(classfile, sizeof(classfile), "%s/%s.class", classpath, classname);

    class_info_t *klass = classfile_load(&g_jvm, classfile);
    if (!klass) {
        printf("[jvm] failed to load: %s\n", classfile);
        return 1;
    }
    printf("[jvm] loaded %s\n", klass->name ? klass->name : classname);

    method_info_t *main_m =
        class_find_method(klass, "main", "([Ljava/lang/String;)V");
    if (!main_m)
        main_m = class_find_method(klass, "main", (char *)0);
    if (!main_m || !main_m->code) {
        printf("[jvm] main() not found in %s\n",
               klass->name ? klass->name : classname);
        return 1;
    }

    int napp_args = argc - 2;
    jobj_t *str_arr = obj_array_ref(&g_jvm, napp_args);
    for (int i = 0; i < napp_args; i++) {
        int slen = (int)strlen(argv[i + 2]);
        jobj_t *s = obj_string(&g_jvm, argv[i + 2], slen);
        if (str_arr && str_arr->rarr.data)
            str_arr->rarr.data[i] = s;
    }

    jval_t main_args[1];
    main_args[0] = JVAL_REF(str_arr);
    interp_exec(&g_jvm, klass, main_m, main_args, 1);

    return 0;
}
