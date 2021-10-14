#!/bin/bash
$(dirname $0)/../../etc/scripts/test_panda.py gcc_regression_simple --tool=bambu \
   --args="--configuration-name=11 -lm --device-name=xc7z020,-1,clg484 --simulate --clock-period=9 --experimental-setup=BAMBU --compiler=I386_GCC49 -O0" \
   --args="--configuration-name=11-bhl -lm --device-name=xc7z020,-1,clg484 --simulate --clock-period=9 --bram-high-latency --experimental-setup=BAMBU --compiler=I386_GCC49 -O0" \
   --args="--configuration-name=N1 -lm --device-name=xc7z020,-1,clg484 --simulate --clock-period=9 --channels-type=MEM_ACC_N1 --experimental-setup=BAMBU --compiler=I386_GCC49 -O0" \
   --args="--configuration-name=N1-bhl -lm --device-name=xc7z020,-1,clg484 --simulate --clock-period=9 --channels-type=MEM_ACC_N1 --bram-high-latency --experimental-setup=BAMBU --compiler=I386_GCC49 -O0" \
   --args="--configuration-name=NN -lm --device-name=xc7z020,-1,clg484 --simulate --clock-period=9 --channels-type=MEM_ACC_NN --experimental-setup=BAMBU --compiler=I386_GCC49 -O0" \
   --args="--configuration-name=NN-bhl -lm --device-name=xc7z020,-1,clg484 --simulate --clock-period=9 --channels-type=MEM_ACC_NN --bram-high-latency --experimental-setup=BAMBU --compiler=I386_GCC49 -O0" \
   -o output_Zynq -b$(dirname $0) --table=output_Zynq.tex  --name="GccRegressionSimpleZynq" "$@"
exit $?
