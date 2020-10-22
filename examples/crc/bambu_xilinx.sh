#!/bin/bash
script=$(readlink -e $0)
root_dir=$(dirname $script)
export PATH=/opt/panda/bin:$PATH

mkdir -p icrc
cd icrc
echo "#synthesis of icrc"
timeout 2h bambu $root_dir/spec.c --top-fname=icrc --simulator=VERILATOR --simulate --generate-tb=$root_dir/test_icrc.xml --experimental-setup=BAMBU --no-iob -v2
return_value=$?
if test $return_value != 0; then
   exit $return_value
fi
cd ..

mkdir -p main
cd main
echo "#synthesis of main"
timeout 2h bambu $root_dir/spec.c  --simulator=VERILATOR --simulate --generate-tb=$root_dir/test.xml --experimental-setup=BAMBU --no-iob -v2
return_value=$?
if test $return_value != 0; then
   exit $return_value
fi


