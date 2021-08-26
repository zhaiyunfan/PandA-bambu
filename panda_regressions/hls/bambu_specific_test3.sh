#!/bin/bash

. $script_dir/generic_getopt.sh

BATCH_ARGS=("-lm" "--evaluation=TOTAL_CYCLES,CYCLES" "--experimental-setup=BAMBU" "--expose-globals" "--compiler=I386_GCC49")
OUT_SUFFIX="bambu_specific_test3"

$(dirname $0)/../../etc/scripts/test_panda.py --tool=bambu  \
   --args="--configuration-name=O3-none --hls-div=none -O3 ${BATCH_ARGS[*]}" \
   --args="--configuration-name=O3-as   --hls-div=as   -O3 ${BATCH_ARGS[*]}" \
   --args="--configuration-name=O3-nr1  --hls-div=nr1  -O3 ${BATCH_ARGS[*]}" \
   --args="--configuration-name=O3-nr2  --hls-div=nr2  -O3 ${BATCH_ARGS[*]}" \
   --args="--configuration-name=O3-NR   --hls-div=NR   -O3 ${BATCH_ARGS[*]}" \
   -lbambu_specific_test3_list \
   -o "output_${OUT_SUFFIX}" -b$(dirname $0) \
   --table="${REPORT_DIR}${OUT_SUFFIX}.tex" \
   --csv="${REPORT_DIR}${OUT_SUFFIX}.csv" \
   --name="${OUT_SUFFIX}" "$@"
exit $?
