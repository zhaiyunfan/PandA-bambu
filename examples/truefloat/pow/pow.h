#include <stdio.h>
#include <stdlib.h>
#include "support.h"

#define TESTS_COUNT 2

DATA_TYPE double_prec_pow(DATA_TYPE, DATA_TYPE);

////////////////////////////////////////////////////////////////////////////////
// Test harness interface code.

struct bench_args_t
{
   double a[TESTS_COUNT];
   double b[TESTS_COUNT];
   double c[TESTS_COUNT];
};