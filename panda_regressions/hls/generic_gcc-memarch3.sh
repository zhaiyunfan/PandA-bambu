#!/bin/bash

script_dir="$(dirname $(readlink -e $0))"
ggo_require_compiler=1
. $script_dir/generic_getopt.sh

BATCH_ARGS=("--simulate" "-O2" "-fwhole-program" "--experimental-setup=BAMBU" "--clock-period=15" "-D'printf(fmt, ...)='")
OUT_SUFFIX="${compiler}_gcc-memarch2"

python3 $script_dir/../../etc/scripts/test_panda.py --tool=bambu \
   --args="--configuration-name=${compiler}-O2-wp-11-aligned-ALL --memory-allocation-policy=ALL_BRAM --channels-type=MEM_ACC_11 --aligned-access ${BATCH_ARGS[*]}"\
   --args="--configuration-name=${compiler}-O2-wp-N1-aligned-ALL --memory-allocation-policy=ALL_BRAM --channels-type=MEM_ACC_N1 --aligned-access ${BATCH_ARGS[*]}"\
   --args="--configuration-name=${compiler}-O2-wp-NN-aligned-ALL --memory-allocation-policy=ALL_BRAM --channels-type=MEM_ACC_NN --aligned-access ${BATCH_ARGS[*]}"\
   --args="--configuration-name=${compiler}-O2-wp-11-unaligned-ALL --memory-allocation-policy=ALL_BRAM --channels-type=MEM_ACC_11 --unaligned-access ${BATCH_ARGS[*]}"\
   --args="--configuration-name=${compiler}-O2-wp-N1-unaligned-ALL --memory-allocation-policy=ALL_BRAM --channels-type=MEM_ACC_N1 --unaligned-access ${BATCH_ARGS[*]}"\
   --args="--configuration-name=${compiler}-O2-wp-NN-unaligned-ALL --memory-allocation-policy=ALL_BRAM --channels-type=MEM_ACC_NN --unaligned-access ${BATCH_ARGS[*]}"\
   --args="--configuration-name=${compiler}-O2-wp-11-aligned-ALL-bhl3 --memory-allocation-policy=ALL_BRAM --channels-type=MEM_ACC_11 --aligned-access --bram-high-latency=3 ${BATCH_ARGS[*]}"\
   --args="--configuration-name=${compiler}-O2-wp-N1-aligned-ALL-bhl3 --memory-allocation-policy=ALL_BRAM --channels-type=MEM_ACC_N1 --aligned-access --bram-high-latency=3 ${BATCH_ARGS[*]}"\
   --args="--configuration-name=${compiler}-O2-wp-NN-aligned-ALL-bhl3 --memory-allocation-policy=ALL_BRAM --channels-type=MEM_ACC_NN --aligned-access --bram-high-latency=3 ${BATCH_ARGS[*]}"\
   --args="--configuration-name=${compiler}-O2-wp-11-unaligned-ALL-bhl3 --memory-allocation-policy=ALL_BRAM --channels-type=MEM_ACC_11 --unaligned-access --bram-high-latency=3 ${BATCH_ARGS[*]}"\
   --args="--configuration-name=${compiler}-O2-wp-N1-unaligned-ALL-bhl3 --memory-allocation-policy=ALL_BRAM --channels-type=MEM_ACC_N1 --unaligned-access --bram-high-latency=3 ${BATCH_ARGS[*]}"\
   --args="--configuration-name=${compiler}-O2-wp-NN-unaligned-ALL-bhl3 --memory-allocation-policy=ALL_BRAM --channels-type=MEM_ACC_NN --unaligned-access --bram-high-latency=3 ${BATCH_ARGS[*]}"\
   --args="--configuration-name=${compiler}-O2-wp-11-aligned-ALL-bhl4 --memory-allocation-policy=ALL_BRAM --channels-type=MEM_ACC_11 --aligned-access --bram-high-latency=4 ${BATCH_ARGS[*]}"\
   --args="--configuration-name=${compiler}-O2-wp-N1-aligned-ALL-bhl4 --memory-allocation-policy=ALL_BRAM --channels-type=MEM_ACC_N1 --aligned-access --bram-high-latency=4 ${BATCH_ARGS[*]}"\
   --args="--configuration-name=${compiler}-O2-wp-NN-aligned-ALL-bhl4 --memory-allocation-policy=ALL_BRAM --channels-type=MEM_ACC_NN --aligned-access --bram-high-latency=4 ${BATCH_ARGS[*]}"\
   --args="--configuration-name=${compiler}-O2-wp-11-unaligned-ALL-bhl4 --memory-allocation-policy=ALL_BRAM --channels-type=MEM_ACC_11 --unaligned-access --bram-high-latency=4 ${BATCH_ARGS[*]}"\
   --args="--configuration-name=${compiler}-O2-wp-N1-unaligned-ALL-bhl4 --memory-allocation-policy=ALL_BRAM --channels-type=MEM_ACC_N1 --unaligned-access --bram-high-latency=4 ${BATCH_ARGS[*]}"\
   --args="--configuration-name=${compiler}-O2-wp-NN-unaligned-ALL-bhl4 --memory-allocation-policy=ALL_BRAM --channels-type=MEM_ACC_NN --unaligned-access --bram-high-latency=4 ${BATCH_ARGS[*]}"\
   --args="--configuration-name=${compiler}-O2-wp-VVD-NN-LSS --memory-allocation-policy=LSS --channels-type=MEM_ACC_NN ${BATCH_ARGS[*]}"\
   --args="--configuration-name=${compiler}-O2-wp-VVD-NN-GSS --memory-allocation-policy=GSS --channels-type=MEM_ACC_NN ${BATCH_ARGS[*]}"\
   -l${script_dir}/chstone_memarch_list2 \
   -o "out_${OUT_SUFFIX}" -b$script_dir \
   --name="${OUT_SUFFIX}" $ARGS
