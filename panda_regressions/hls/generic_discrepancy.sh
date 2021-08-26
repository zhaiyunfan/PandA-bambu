#!/bin/bash

script_dir="$(dirname $(readlink -e $0))"
ggo_require_compiler=1
. $script_dir/generic_getopt.sh

BATCH_ARGS=("-lm" "--simulate" "--experimental-setup=BAMBU" "--expose-globals" "--discrepancy")
OUT_SUFFIX="${COMPILER}_grs_discrepancy"

$script_dir/../../etc/scripts/test_panda.py --tool=bambu  \
   --args="--configuration-name=${COMPILER}_O2 -O2 ${BATCH_ARGS[*]}" \
   -ldiscrepancy_list \
   -o "output_${OUT_SUFFIX}" -b$script_dir \
   --table="${REPORT_DIR}${OUT_SUFFIX}.tex" \
   --csv="${REPORT_DIR}${OUT_SUFFIX}.csv" \
   --name="${OUT_SUFFIX}" $ARGS
exit $?
