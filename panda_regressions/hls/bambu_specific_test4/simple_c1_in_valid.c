#pragma HLS_interface a valid
void sum3numbers(short *a, short b, short c, short *d)
{
  *d = *a + b + c;
}
