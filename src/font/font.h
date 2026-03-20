#ifndef FONT_H
#define FONT_H

#include <stdint.h>

/*
 * font.h – 커널 폰트 렌더링 API
 *
 * 두 가지 폰트 백엔드를 지원합니다:
 *  1. BIOS 8×8 비트맵 폰트 (boot.asm 이 0x9100 에 포인터를 저장)
 *  2. stb_truetype 으로 렌더링하는 TrueType 폰트 (filesystem 에서 로드)
 */

/* 폰트 시스템 초기화: BIOS 8×8 내장 폰트를 활성화합니다. */
void font_init(void);

/*
 * 커널에 내장된 FONT.TTF 심볼을 로드하여 활성화합니다.
 * @param size  픽셀 단위 글자 높이
 * @return  0=성공, 음수=실패
 */
int font_load_embedded_ttf(int size);

/*
 * TrueType 폰트 파일을 파일시스템에서 로드하여 활성화합니다.
 * @param path  파일시스템 절대 경로 (예: "/0/fonts/mono.ttf")
 * @param size  픽셀 단위 글자 높이
 * @return  0=성공, 음수=실패
 */
int font_load_ttf(const char *path, int size);

/* 폰트가 준비되어 있으면 1, 아니면 0 반환 */
int font_is_ready(void);

/* 현재 활성 폰트의 글자 너비/높이 반환 (픽셀) */
int font_get_width(void);
int font_get_height(void);

/*
 * VESA 프레임버퍼에 ASCII 문자 하나를 렌더링합니다.
 * @param px, py        화면 왼쪽 위 픽셀 좌표
 * @param c             출력할 문자 (ASCII 0–127)
 * @param fr,fg,fb      전경색 RGB
 * @param br,bg_c,bb    배경색 RGB
 */
void font_draw_char(int px, int py, uint32_t codepoint,
                    uint8_t fr, uint8_t fg, uint8_t fb,
                    uint8_t br, uint8_t bg_c, uint8_t bb);

#endif /* FONT_H */
