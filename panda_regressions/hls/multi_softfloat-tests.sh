#!/bin/bash

. $script_dir/generic_getopt.sh

script_dir="$(dirname $(readlink -e $0))"

BATCH_ARGS=("--soft-float" "--max-ulp=0" "--experimental-setup=BAMBU-PERFORMANCE-MP")
OUT_SUFFIX="multi_softfloat-tests"

$script_dir/../../etc/scripts/test_panda.py --tool=bambu \
   --args="--configuration-name=GCC49   --compiler=I386_GCC49   ${BATCH_ARGS[*]}" \
   --args="--configuration-name=CLANG4  --compiler=I386_CLANG4  ${BATCH_ARGS[*]}" \
   --args="--configuration-name=CLANG5  --compiler=I386_CLANG5  ${BATCH_ARGS[*]}" \
   --args="--configuration-name=CLANG6  --compiler=I386_CLANG6  ${BATCH_ARGS[*]}" \
   --args="--configuration-name=CLANG7  --compiler=I386_CLANG7  ${BATCH_ARGS[*]}" \
   --args="--configuration-name=CLANG11 --compiler=I386_CLANG11 ${BATCH_ARGS[*]}" \
   -lsoftfloat-tests_list \
   -o "output_${OUT_SUFFIX}" -b$script_dir \
   --table="${REPORT_DIR}${OUT_SUFFIX}.tex" \
   --csv="${REPORT_DIR}${OUT_SUFFIX}.csv" \
   --name="${OUT_SUFFIX}" $@
exit $?
