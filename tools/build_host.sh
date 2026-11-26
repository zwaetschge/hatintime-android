#!/bin/sh
# Linux debug host build: every gen/<Module>/*.c with its own table names, the runtime, host/main.c
# -> build/host/hathost. Incremental (objects newer than their source are kept). env: JOBS, OPT
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
J=${JOBS:-14}; O=${OPT:--O1}
python3 tools/gen_modules.py ref/lift/layout.json gen/modules_table.c
F="-mcx16 -w -fwrapv -fno-strict-aliasing -ffp-contract=off -g0 -fno-unwind-tables -fno-asynchronous-unwind-tables -Iruntime/include"
mkdir -p build/host/rt
: > build/host/todo.txt
for d in gen/*/; do
  M=$(basename $d)
  mkdir -p build/host/$M
  for f in $d*.c; do
    o=build/host/$M/$(basename $f .c).o
    if [ ! -f $o ] || [ $f -nt $o ]; then
      u=$M/$(basename $f .c); OO=$O; grep -qx "$u" config/o0_host.txt 2>/dev/null && OO=-O0
      echo "$OO $f $o -I$d -Drecomp_dispatch_table=${M}_dispatch_table -Drecomp_dispatch_count=${M}_dispatch_count -Drecomp_imports=${M}_imports" >> build/host/todo.txt
    fi
  done
done
echo "$(wc -l < build/host/todo.txt) generated units to compile"
[ -s build/host/todo.txt ] && cat build/host/todo.txt | xargs -P $J -L 1 sh -c 'clang $0 '"$F"' -c $1 -o $2 $3 $4 $5 $6'
RT="-O1 -g -Wall -Wno-unused-function -Wno-unused-variable -fwrapv -fno-strict-aliasing -ffp-contract=off -Iruntime/include -Iruntime/src"
for f in runtime/src/*.c runtime/include/eh64.c host/main.c gen/modules_table.c; do
  o=build/host/rt/$(basename $f .c).o
  if [ ! -f $o ] || [ $f -nt $o ] || [ runtime/src/w64.h -nt $o ] || [ runtime/include/cpu64.h -nt $o ]; then clang $RT -c $f -o $o; fi
done
# Direct3D 9 bridge over DXVK native (tools/build_dxvk.sh host builds build/dxvk-host/libhp_dxvk.a)
D=build/dxvk-src
DX="-DDXVK_NATIVE -DDXVK_PLATFORM_LINUX -DNOMINMAX -I$D -I$D/include -I$D/src/util -I$D/include/native -I$D/include/native/windows -I$D/include/native/directx -I$D/generated"
for f in gfx/hat_d3d9.cpp; do
  o=build/host/rt/$(basename $f .cpp).o
  if [ ! -f $o ] || [ $f -nt $o ]; then clang++ -std=c++17 -O1 -g -w $DX -c $f -o $o; fi
done
clang++ -o build/host/hathost build/host/rt/*.o $(ls build/host/*/recomp_*.o) build/dxvk-host/libhp_dxvk.a -lvulkan -lX11 -lpthread -ldl -lm
ls -la build/host/hathost
