#include "ac_channel.h"
#include "ap_int.h"

#pragma HLS interface port=a mode=fifo depth=8
#pragma HLS interface port=b mode=fifo depth=8
#pragma HLS interface port=c mode=fifo depth=8
#pragma HLS interface port=d mode=fifo depth=8
void sum3numbers(ac_channel<ap_uint<16>>& a, ac_channel<ap_uint<16>>& b, ac_channel<ap_uint<16>>& c,
                 ac_channel<ap_uint<16>>& d)
{
   int i;
#pragma nounroll
   for(i = 0; i < 8; ++i)
      d.write(a.read() + b.read() + c.read());
}
