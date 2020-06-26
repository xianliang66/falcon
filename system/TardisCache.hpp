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

struct cache_info {
  cache_info() : rts(0), wts(0), refcnt(0), size(0), object(nullptr) {}
  cache_info(timestamp_t _rts, timestamp_t _wts) : rts(_rts), wts(_wts),
    refcnt(0), size(0), object(nullptr) {}
  cache_info(void* obj, size_t sz) : object(obj), size(sz), rts(0), wts(0),
    refcnt(0) {}
  mutable timestamp_t rts, wts;
  mutable char refcnt;
  mutable size_t size;
  mutable void* object;

  void assign(void* obj) {
    memcpy(object, obj, size);
  }
  void* get_object() { return object; }
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
