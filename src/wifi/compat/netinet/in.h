#pragma once
#ifdef _WIN32
#include "../../net_compat.h"
#else
#include_next <netinet/in.h>
#endif
