#!/bin/bash
$(dirname $0)/../../etc/scripts/test_panda.py gcc_regression_simple --tool=bambu -c="--memory-allocation-policy=EXT_PIPELINED_BRAM"\
   --args="--configuration-name=CLANG10_O0 -O0 -lm --simulate --experimental-setup=BAMBU --expose-globals --compiler=I386_CLANG10 --channels-type=MEM_ACC_NN" \
   --args="--configuration-name=CLANG10_O1 -O1 -lm --simulate --experimental-setup=BAMBU --expose-globals --compiler=I386_CLANG10 --channels-type=MEM_ACC_NN" \
   --args="--configuration-name=CLANG10_O2 -O2 -lm --simulate --experimental-setup=BAMBU --expose-globals --compiler=I386_CLANG10 --channels-type=MEM_ACC_NN" \
   --args="--configuration-name=CLANG10_O3 -O3 -lm --simulate --experimental-setup=BAMBU --expose-globals --compiler=I386_CLANG10 --channels-type=MEM_ACC_NN" \
   -o output_clang10_simple_ext_pipelined -b$(dirname $0) --table=output_clang10_simple_ext_pipelined.tex --name="Clang10RegressionSimple-ExtPipelined" $@
exit $?
