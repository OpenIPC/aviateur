#pragma once
#ifdef _WIN32
#include "../../net_compat.h"
#else
#include_next <sys/socket.h>
#endif
