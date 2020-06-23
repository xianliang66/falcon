#pragma once
#include <iostream>

/// Whether the delegate operations use Tardis-based cache.
#define GRAPPA_TARDIS_CACHE

#ifdef GRAPPA_TARDIS_CACHE
#define LEASE 10
#define MAX_CACHE_NUMBER 24 

typedef uint32_t timestamp_t;
typedef int16_t Core;

namespace Grappa {
namespace impl {

struct owner_cache_info {
  owner_cache_info() : rts(0), wts(0) {}
  mutable timestamp_t rts, wts;
};

template< typename T >
struct cache_info {
  cache_info() : rts(0), wts(0) {}
  cache_info(timestamp_t _rts, timestamp_t _wts) : rts(_rts), wts(_wts) {}
  mutable timestamp_t rts, wts;
  mutable T object;
};

template< typename T >
struct rpc_read_result {
  rpc_read_result(T _r, const owner_cache_info& c) : r(_r), rts(c.rts), wts(c.wts) {}
  rpc_read_result() {}
  timestamp_t rts, wts;
  T r;
};

}
}
#endif
