#pragma once

#include <version>

#if __cpp_lib_expected >= 202202L && __cplusplus >= 202302L
#define CORAL_EXPECTED 1
#else
#define CORAL_EXPECTED 0
#endif
