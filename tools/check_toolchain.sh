#!/bin/bash
# OpenJazz CD32X J1 - what the Sega toolchain offers for C++ (OpenJazz is C++14 and uses
# exceptions for its error paths).  Paste the whole output back.
T=/opt/toolchains/sega
echo "== compilers";   ls $T/sh-elf/bin/ 2>/dev/null | grep -E 'gcc|g\+\+|c\+\+' || echo "no $T/sh-elf/bin"
echo "== C++ libraries"; find $T -name 'libstdc++*' -o -name 'libsupc++*' 2>/dev/null | head
echo "== newlib";      find $T -path '*sh-elf*' -name 'libc.a' 2>/dev/null | head -4
G=$T/sh-elf/bin/sh-elf-g++
if [ -x $G ]; then
  echo "== test: exceptions + unique_ptr + list, -m2 -mb -Os"
  cat > /tmp/ojtc.cpp <<'CPP'
#include <memory>
#include <list>
int f(int x) { if (x < 0) throw x; return x; }
int main() { std::list<int> l; auto p = std::make_unique<int>(3); l.push_back(*p);
  try { return f(-l.front()); } catch (int e) { return e; } }
CPP
  $G -m2 -mb -Os -std=gnu++14 -c /tmp/ojtc.cpp -o /tmp/ojtc.o && echo "compile OK" && $T/sh-elf/bin/sh-elf-size /tmp/ojtc.o
  $G -m2 -mb -Os -std=gnu++14 /tmp/ojtc.cpp -o /tmp/ojtc.elf -nostartfiles -Wl,-e,main 2>&1 | head -5 && \
    echo "link OK" && $T/sh-elf/bin/sh-elf-size /tmp/ojtc.elf
fi
