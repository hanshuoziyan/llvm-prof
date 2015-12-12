#include "Profiling.h"
#include <stdlib.h>

static double *ArrayStart;
static uint64_t NumElements;

static void PredBlockProfAtExitHandler(void) {
  write_profiling_data_double(BlockInfoDouble, ArrayStart, NumElements);
}

int llvm_start_pred_double_block_profiling(int argc, const char** argv,
                                    double* arrayStart, uint64_t numElements)
{
  int Ret = save_arguments(argc, argv);
  ArrayStart = arrayStart;
  NumElements = numElements;
  atexit(PredBlockProfAtExitHandler);
  return Ret;
}
