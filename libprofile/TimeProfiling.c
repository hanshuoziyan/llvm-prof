#include "Profiling.h"
#include <stdlib.h>

static double *ArrayStart;
static uint64_t NumElements;

static void TimeProfAtExitHandler(void) {
  write_profiling_data_double(MPITimeInfo, ArrayStart, NumElements);
}

int llvm_start_time_profiling(int argc, const char** argv,
                                    double* arrayStart, uint64_t numElements)
{
  int Ret = save_arguments(argc, argv);
  ArrayStart = arrayStart;
  NumElements = numElements;
  atexit(TimeProfAtExitHandler);
  return Ret;
}
