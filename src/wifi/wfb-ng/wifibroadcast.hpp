#pragma once

// WFB-NG Proxy Header for MSVC Struct Packing
// This file ensures that the protocol structures are 1-byte aligned on Windows
// while keeping the official source code untouched.

#ifdef _MSC_VER
#pragma pack(push, 1)
#endif

// Include the real protocol definitions from the subdirectory
#include "protocol/wifibroadcast.hpp"

#ifdef _MSC_VER
#pragma pack(pop)
#endif
