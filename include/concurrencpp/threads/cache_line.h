#ifndef CONCURRENCPP_CACHE_LINE_H
#define CONCURRENCPP_CACHE_LINE_H

#include <new>

#ifdef __cpp_lib_hardware_interference_size
#    define CRCPP_CACHE_LINE_ALIGNMENT std::hardware_destructive_interference_size
#else
#    define CRCPP_CACHE_LINE_ALIGNMENT 64
#endif

#endif