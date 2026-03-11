; 2단계 C 로더 진입점 (32비트 보호 모드에서 실행)
; 부트로더(boot.asm)가 0x10000에 로드한 뒤 여기로 점프

[bits 32]
extern loader_main      ; C 로더의 loader_main 함수
global _loader_entry

_loader_entry:
    ; 스택은 부트로더(boot.asm)에서 이미 0x90000으로 설정됨
    call loader_main    ; C 로더 진입
    jmp $               ; 리턴 시 무한 루프 (정상적으로는 도달하지 않음)
