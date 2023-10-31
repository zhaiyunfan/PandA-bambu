#!/bin/bash
abs_script=$(readlink -e $0)
dir_script=$(dirname $abs_script)
$dir_script/../../test_panda.py --tool=bambu --bambu=bambu --spider=spider \
   --args="--configuration-name=GCC49 --compiler=I386_GCC49" \
   --args="--configuration-name=GCC7 --compiler=I386_GCC7" \
   --args="--configuration-name=CLANG12 --compiler=I386_CLANG12" \
   --args="--configuration-name=CLANG13 --compiler=I386_CLANG13" \
   -c=--simulate -b$dir_script -l$dir_script/list "$@"
