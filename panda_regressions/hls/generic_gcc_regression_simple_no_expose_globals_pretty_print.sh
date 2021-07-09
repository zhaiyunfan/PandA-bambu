#!/bin/bash

script_dir="$(dirname $(readlink -e $0))"
. $script_dir/generic_getopt.sh

BATCH_ARGS=("-lm" "--simulate" "--experimental-setup=BAMBU" "--do-not-expose-globals" "--pretty-print=output.c")
OUT_SUFFIX="${COMPILER}_grs_no_expose_globals_pretty_preint"

$script_dir/../../etc/scripts/test_panda.py --tool=bambu \
   --args="--configuration-name=${COMPILER}_O0 -O0 ${BATCH_ARGS[*]}" \
   --args="--configuration-name=${COMPILER}_O1 -O1 ${BATCH_ARGS[*]}" \
   --args="--configuration-name=${COMPILER}_O2 -O2 ${BATCH_ARGS[*]}" \
   --args="--configuration-name=${COMPILER}_O3 -O3 ${BATCH_ARGS[*]}" \
   -ldiscrepancy_list \
   -o "output_${OUT_SUFFIX}" -b$script_dir \
   --table="${OUT_SUFFIX}.tex" \
   --csv="${OUT_SUFFIX}.csv" \
   --name="${OUT_SUFFIX}" $ARGS
exit $?
