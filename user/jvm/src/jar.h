/*
 * user/jvm/src/jar.h — JAR 파일 파서 API
 */

#ifndef PARINOS_JAR_H
#define PARINOS_JAR_H

#include <stdint.h>
#include "jvm.h"

/**
 * JAR 파일을 열고 메모리에 로드합니다.
 * @return 0 성공, -1 실패
 */
int jar_open(const char *path);

/** JAR 파일을 닫고 메모리를 해제합니다. */
void jar_close(void);

/**
 * META-INF/MANIFEST.MF 에서 Main-Class 항목을 읽습니다.
 * 클래스 이름의 '.'은 '/'로 변환됩니다 (내부 이름 형식).
 * @return 0 성공, -1 Main-Class 없음
 */
int jar_read_main_class(char *out_class, int max_len);

/**
 * JAR 내의 클래스를 로드합니다.
 * @param class_name  내부 클래스 이름 (예: "com/example/Hello")
 * @return class_info_t* 또는 NULL
 */
class_info_t *jar_load_class(jvm_t *jvm, const char *class_name);

/** JAR 내 .class 파일 목록을 표준출력에 출력합니다. */
void jar_list(void);

#endif /* PARINOS_JAR_H */
