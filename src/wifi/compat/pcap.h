#pragma once

#ifdef _WIN32
// Stubs already in net_compat.h via PREINCLUDE_FILE
#else
#include_next <pcap.h>
#endif
