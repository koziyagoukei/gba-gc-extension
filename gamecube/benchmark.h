#ifndef POC_BENCHMARK_H
#define POC_BENCHMARK_H
#include "session.h"

bool ping(Session *session, uint32_t pattern, Frame *reply, enum Fault fault);
bool reliability_test(Session *session, unsigned interval_us, bool faults);
#endif
