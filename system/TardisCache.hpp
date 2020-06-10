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

template< typename T >
struct cache_info {
  cache_info() : valid(false), rts(0), wts(0) {}
  cache_info(timestamp_t _rts, timestamp_t _wts) : rts(_rts), wts(_wts), valid(false) {}
  mutable timestamp_t rts, wts;
  mutable Core core;
  mutable bool valid;
  mutable T object;
};

template< typename T >
struct rpc_read_result {
  rpc_read_result(T _r, cache_info<T>& c) : r(_r), rts(c.rts), wts(c.wts) {}
  rpc_read_result() {}
  timestamp_t rts, wts;
  T r;
};

}
}
#endif
