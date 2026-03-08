global gdt_flush    ; C 코드에서 이 함수를 찾을 수 있도록 global로 선언합니다.

gdt_flush:
    ; 1. C 함수에서 넘어온 첫 번째 인자(gdt_ptr의 주소)를 가져옵니다.
    ; C언어의 호출 규약(cdecl)에 의해 첫 번째 인자는 스택의 [esp+4]에 위치합니다.
    mov eax, [esp+4]
    
    ; 2. CPU의 GDTR 레지스터에 새로운 GDT 주소를 로드합니다.
    lgdt [eax]

    ; 3. 데이터 세그먼트 레지스터들을 갱신합니다.
    ; gdt.c에서 데이터 세그먼트는 3번째 엔트리(인덱스 2)에 만들었습니다.
    ; 엔트리 하나당 8바이트이므로, 2 * 8 = 16 = 0x10 입니다.
    mov ax, 0x10      
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; 4. 코드 세그먼트(CS) 레지스터 갱신
    ; CS 레지스터는 mov 명령어로 직접 바꿀 수 없습니다. 
    ; 대신 Far Jump(jmp 세그먼트:오프셋)를 사용해서 강제로 갱신해야 합니다.
    ; 코드 세그먼트는 2번째 엔트리(인덱스 1)이므로 1 * 8 = 0x08 입니다.
    jmp 0x08:.flush   

.flush:
    ; 파이프라인이 비워지고 CS가 0x08로 갱신되었습니다.
    ; 이제 C 언어 코드로 다시 돌아갑니다.
    ret