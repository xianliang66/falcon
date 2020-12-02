#pragma once
#include <bitset>
#include <list>
#include <unordered_map>
#include <gflags/gflags.h>
#include <string.h>

//#define TARDIS_TWO_STAGE_RENEWAL
//#ifdef TARDIS_BG_RENEWAL
// Cache protocol. Only one of them can be defined
enum cache_proto_t { GRAPPA_VANILLA = 0, GRAPPA_TARDIS, GRAPPA_WI };
static const char* cache_proto_str[] =  { "Vanilla", "Tardis", "Write-Invalidation" };
// The definiations are given in TardisCache.cpp
DECLARE_int32(cache_proto);
DECLARE_int32(max_cache_number);
DECLARE_int32(lease);
static const int MAX_NODE_NUMBER = 160;

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
  mutable uint16_t size;
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

struct tardis_owner_cache_info {
  tardis_owner_cache_info() : rts(0), wts(0), lease(1) {}
  timestamp_t rts, wts;
  unsigned char lease;
};

struct tardis_cache_info : cache_info_base {
  tardis_cache_info() : cache_info_base() {}
  tardis_cache_info(void *obj, size_t sz) : cache_info_base(obj, sz),
    rts(0), wts(0) {}
  mutable timestamp_t rts, wts;
};

template< typename T >
struct rpc_read_result {
  rpc_read_result(T _r, const tardis_owner_cache_info& c) : r(_r), rts(c.rts),
    wts(c.wts) {}
  rpc_read_result() {}
  timestamp_t rts, wts;
  T r;
};

struct wi_owner_cache_info {
  std::bitset<MAX_NODE_NUMBER> copyset;
  // Whether this object is locked globally.
  bool locked;
  wi_owner_cache_info(): locked(false) {}
};

struct wi_cache_info : cache_info_base {
  bool valid;
  wi_cache_info() : cache_info_base(), valid(false) {}
  wi_cache_info(void *obj, size_t sz) : cache_info_base(obj, sz),
    valid(false) {}
};

}
}
using tardis_o_t = Grappa::impl::tardis_owner_cache_info;
using wi_o_t = Grappa::impl::wi_owner_cache_info;
using tardis_c_t = Grappa::impl::tardis_cache_info;
using wi_c_t = Grappa::impl::wi_cache_info;

/// C++ template is hard to use!!!
namespace GlobalCacheData {
  static std::unordered_map<uintptr_t, tardis_o_t> tardis_owner_cache;
  static std::unordered_map<uintptr_t, wi_o_t> wi_owner_cache;
  static std::unordered_map<uintptr_t, tardis_c_t> tardis_cache;
  static std::unordered_map<uintptr_t, wi_c_t> wi_cache;
  static std::list<uintptr_t> lru;
};
