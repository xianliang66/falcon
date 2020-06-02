#include "Addressing.hpp"
#include <unistd.h>

#ifdef GRAPPA_TARDIS_CACHE

std::map<void*,Grappa::impl::cache_info> cache;

void invalidate_core( Core dest ) {
  // DO NOT TOUCH it->first
  int cnt = 0;
  for (auto it = cache.begin(); it != cache.end(); it++) {
    if (dest != Grappa::mycore() && it->second.core == dest) {
      it->second.valid = false;
      cnt++;
    }
  }
}

void write_core() {
  // DO NOT TOUCH it->first
  for (auto it = cache.begin(); it != cache.end(); it++) {
    if (it->second.core == Grappa::mycore()) {
      it->second.wts = std::max<timestamp_t>(it->second.rts + 1, Grappa::mypts());
      it->second.rts = Grappa::mypts() = it->second.wts;
    }
  }
}

#endif

