#pragma once
#include <iostream>

/// Whether the delegate operations use Tardis-based cache.
#define GRAPPA_TARDIS_CACHE

#ifdef GRAPPA_TARDIS_CACHE
#define LEASE 10
typedef uint32_t timestamp_t;

namespace Grappa {
namespace impl {

struct cache_info {
  cache_info() : rts(0), wts(0), valid(false) {}
  cache_info(timestamp_t _rts, timestamp_t _wts) : rts(_rts), wts(_wts), valid(false) {}
  mutable timestamp_t rts, wts;
  mutable bool valid;
};

template< typename T >
struct cache_result {
  cache_result(T _r, cache_info _cache) : r(_r), cache(_cache) {}
  cache_result() {}
  T r;
  cache_info cache;
};
}
}
#endif
