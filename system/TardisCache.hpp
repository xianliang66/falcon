#pragma once
#include <iostream>

/// Whether the delegate operations use Tardis-based cache.
#define GRAPPA_TARDIS_CACHE

#ifdef GRAPPA_TARDIS_CACHE
#define LEASE 10
typedef uint32_t timestamp_t;
typedef int16_t Core;

namespace Grappa {
namespace impl {

struct cache_info {
  cache_info() {}
  cache_info(timestamp_t _rts, timestamp_t _wts) : rts(_rts), wts(_wts), valid(false) {}
  mutable timestamp_t rts, wts;
  mutable Core core;
  mutable bool valid;
};

template< typename T >
struct cache_result {
  cache_result(T _r, cache_info& c) : r(_r), cache(c) {}
  cache_result() {}
  cache_info cache;
  T r;
};
}
}
#endif
