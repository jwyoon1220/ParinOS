import os, re

def replace_includes(path, is_src_root):
    with open(path, 'r', encoding='utf-8') as f:
        content = f.read()
    
    if is_src_root:
        content = re.sub(r'#include "(vga|keyboard|gdt|idt|io)\.h"', r'#include "hal/\1.h"', content)
    else:
        content = re.sub(r'#include "\.\./(vga|keyboard|gdt|idt|io)\.h"', r'#include "../hal/\1.h"', content)
        
    with open(path, 'w', encoding='utf-8') as f:
        f.write(content)

src_dir = 'src'
for root, dirs, files in os.walk(src_dir):
    for f in files:
        if f.endswith('.c') or f.endswith('.h') or f.endswith('.cpp'):
            path = os.path.join(root, f)
            norm_path = path.replace('\\', '/')
            if 'src/hal' in norm_path:
                # In hal dir, fix ../vga.h to vga.h
                with open(path, 'r', encoding='utf-8') as file:
                    c = file.read()
                c = re.sub(r'#include "\.\./(vga|keyboard|gdt|idt|io)\.h"', r'#include "\1.h"', c)
                with open(path, 'w', encoding='utf-8') as file:
                    file.write(c)
            else:
                is_root = (norm_path.count('/') == 1) # 'src/file.c' has 1 slash
                replace_includes(path, is_root)

print("Replacement complete.")
