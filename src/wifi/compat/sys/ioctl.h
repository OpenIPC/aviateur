#pragma once
// Dummy header for Windows compatibility
#ifdef _WIN32
#include "../../net_compat.h"
#else
#include_next <sys/ioctl.h>
#endif
