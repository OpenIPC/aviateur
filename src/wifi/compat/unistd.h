#pragma once
#ifdef _WIN32
#include "../net_compat.h"
#include <io.h>
#else
#include_next <unistd.h>
#endif
