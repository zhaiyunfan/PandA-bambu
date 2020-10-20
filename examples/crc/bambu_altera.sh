#!/bin/bash
export PATH=/opt/panda/bin:$PATH

mkdir -p icrc
cd icrc
echo "#synthesis of icrc"
timeout 2h bambu ../spec.c --top-fname=icrc --simulator=VERILATOR --device-name=EP2C70F896C6-R --simulate --generate-tb=../test_icrc.xml --experimental-setup=BAMBU -v2
return_value=$?
if test $return_value != 0; then
   exit $return_value
fi
cd ..

mkdir -p main
cd main
echo "#synthesis of main"
timeout 2h bambu ../spec.c  --simulator=VERILATOR  --device-name=EP2C70F896C6-R --simulate --generate-tb=../test.xml --experimental-setup=BAMBU -v2
return_value=$?
if test $return_value != 0; then
   exit $return_value
fi


