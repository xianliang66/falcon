#pragma once
#include <bitset>
#include <list>

// Cache protocol. Only one of them can be defined
#define GRAPPA_TARDIS_CACHE
//#define GRAPPA_WI_CACHE

#if (defined(GRAPPA_TARDIS_CACHE) || defined(GRAPPA_WI_CACHE))
#define GRAPPA_CACHE_ENABLE
#define MAX_NODE_NUM 160 
#endif

#if (defined(GRAPPA_TARDIS_CACHE))

//#define TARDIS_BG_RENEWAL
//#define TARDIS_TWO_STAGE_RENEWAL

#if (defined(TARDIS_BG_RENEWAL) && defined(TARDIS_TWO_STAGE_RENEWAL))
#define GRAPPA_CC_PROTOCOL_NAME "Tardis (BG_RENEWAL|TWO_STAGE_RENEWAL)"
#elif (defined(TARDIS_BG_RENEWAL))
#define GRAPPA_CC_PROTOCOL_NAME "Tardis (BG_RENEWAL)"
#elif (defined(TARDIS_TWO_STAGE_RENEWAL))
#define GRAPPA_CC_PROTOCOL_NAME "Tardis (TWO_STAGE_RENEWAL)"
#else
#define GRAPPA_CC_PROTOCOL_NAME "Tardis"
#endif

#elif (defined(GRAPPA_WI_CACHE))
#define GRAPPA_CC_PROTOCOL_NAME "Write-Invalidation"
#else
#define GRAPPA_CC_PROTOCOL_NAME "Vanilla"
#define MAX_CACHE_NUMBER 0
#endif

#ifdef GRAPPA_CACHE_ENABLE

#define LEASE 10
#define MAX_CACHE_NUMBER 38486 

typedef uint32_t timestamp_t;

namespace Grappa {
namespace impl {

struct cache_info_base {
  cache_info_base() : refcnt(0), usedcnt(0), size(0), object(nullptr) {}
  cache_info_base(void* obj, size_t sz) : object(obj), size(sz),
    refcnt(0), usedcnt(0) {}
  // How many actived tasks (might access internal data of cache_info) are there?
  mutable char refcnt;
  // How many tasks who holds the reference to cache_info are there?
  mutable char usedcnt;
  // These two fields implements template parameters.
  mutable size_t size;
  mutable void* object;
  // O(1) remove/insertion time for LRU list.
  mutable std::list<uintptr_t>::iterator lru_iter;

  void assign(const void* obj) {
    // This object has been freed.
    if (object == nullptr) return;
    memcpy(object, obj, size);
  }
  void* get_object() { return object; }
};

template <typename T>
struct lock_obj { T object; bool locked; };

#ifdef GRAPPA_TARDIS_CACHE
struct owner_cache_info {
  owner_cache_info() : rts(0), wts(0) {}
  mutable timestamp_t rts, wts;
};

struct cache_info : cache_info_base {
  cache_info() : cache_info_base() {}
  cache_info(void *obj, size_t sz) : cache_info_base(obj, sz),
    rts(0), wts(0) {}
  mutable timestamp_t rts, wts;
};

template< typename T >
struct rpc_read_result {
  rpc_read_result(T _r, const owner_cache_info& c) : r(_r), rts(c.rts), wts(c.wts) {}
  rpc_read_result() {}
  timestamp_t rts, wts;
  T r;
};
template< typename T >
struct rpc_read_result2 {
  rpc_read_result2(T _new, T _old, const owner_cache_info& c) :
    new_r(_new), old_r(_old), rts(c.rts), wts(c.wts) {}
  rpc_read_result2() {}
  timestamp_t rts, wts;
  T new_r, old_r;
};
#endif // GRAPPA_TARDIS_CACHE

#if (defined(GRAPPA_WI_CACHE))
struct owner_cache_info {
  std::bitset<MAX_NODE_NUM> copyset;
  // Whether this object is locked globally.
  bool locked;
  owner_cache_info(): locked(false) {}
};

struct cache_info : cache_info_base {
  bool valid;
  cache_info() : cache_info_base(), valid(false) {}
  cache_info(void *obj, size_t sz) : cache_info_base(obj, sz), valid(false) {}
};

template< typename T >
struct rpc_read_result2 {
  rpc_read_result2(T _new, T _old) :
    new_r(_new), old_r(_old) {}
  rpc_read_result2() {}
  T new_r, old_r;
};
#endif // GRAPPA_WI_CACHE

}
}

#endif // GRAPPA_CACHE_ENABLE
