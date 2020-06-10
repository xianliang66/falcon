#include "Addressing.hpp"
#include "GlobalAllocator.hpp"
#include <unistd.h>

#ifdef GRAPPA_TARDIS_CACHE

// rawptr => template <T> cache_info
std::map<void*,void*> tardis_cache;

#endif

