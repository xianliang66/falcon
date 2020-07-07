////////////////////////////////////////////////////////////////////////
// Copyright (c) 2010-2015, University of Washington and Battelle
// Memorial Institute.  All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//     * Redistributions of source code must retain the above
//       copyright notice, this list of conditions and the following
//       disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials
//       provided with the distribution.
//     * Neither the name of the University of Washington, Battelle
//       Memorial Institute, or the names of their contributors may be
//       used to endorse or promote products derived from this
//       software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
// FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
// UNIVERSITY OF WASHINGTON OR BATTELLE MEMORIAL INSTITUTE BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
// OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
// BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
// USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
// DAMAGE.
////////////////////////////////////////////////////////////////////////

#pragma once

#include "Message.hpp"
#include "RDMAAggregator.hpp"
#include "FullEmptyLocal.hpp"
#include "ConditionVariable.hpp"
#include "DelegateBase.hpp"
#include "GlobalCompletionEvent.hpp"
#include "AsyncDelegate.hpp"
#include "TardisCache.hpp"
#include "Communicator.hpp"
#include <type_traits>
#include "Mutex.hpp"

GRAPPA_DECLARE_METRIC(SummarizingMetric<uint64_t>, flat_combiner_fetch_and_add_amount);
GRAPPA_DECLARE_METRIC(SimpleMetric<uint64_t>, delegate_reads);
GRAPPA_DECLARE_METRIC(SimpleMetric<uint64_t>, delegate_read_targets);
GRAPPA_DECLARE_METRIC(SimpleMetric<uint64_t>, delegate_writes);
GRAPPA_DECLARE_METRIC(SimpleMetric<uint64_t>, delegate_write_targets);
GRAPPA_DECLARE_METRIC(SimpleMetric<uint64_t>, delegate_cmpswaps);
GRAPPA_DECLARE_METRIC(SimpleMetric<uint64_t>, delegate_cmpswap_targets);
GRAPPA_DECLARE_METRIC(SimpleMetric<uint64_t>, delegate_fetchadds);
GRAPPA_DECLARE_METRIC(SimpleMetric<uint64_t>, delegate_fetchadd_targets);
GRAPPA_DECLARE_METRIC(SimpleMetric<uint64_t>, delegate_cache_hit);
GRAPPA_DECLARE_METRIC(SimpleMetric<uint64_t>, delegate_cache_miss);
GRAPPA_DECLARE_METRIC(SimpleMetric<uint64_t>, delegate_cache_expired);
GRAPPA_DECLARE_METRIC(SimpleMetric<uint64_t>, delegate_call_all);
GRAPPA_DECLARE_METRIC(SimpleMetric<uint64_t>, delegate_call_one);
GRAPPA_DECLARE_METRIC(SimpleMetric<uint64_t>, delegate_fast_path);


namespace Grappa {
    /// @addtogroup Delegates
    /// @{

  namespace impl {

    template< SyncMode S, GlobalCompletionEvent * C, typename F >
    struct Specializer {
      // async call with void return type
      static void call(Core dest, F func, void (F::*mf)() const) {
        delegate_ops++;
        delegate_async_ops++;
        Core origin = Grappa::mycore();

        if (dest == origin) {
          // short-circuit if local
          delegate_targets++;
          delegate_short_circuits++;
          func();
        } else {
          if (C) C->enroll();

#ifdef ENABLE_NT_MESSAGE
          Grappa::impl::global_rdma_aggregator.send_nt_message(dest, [origin, func] {
#else
          send_heap_message(dest, [origin, func] {
#endif
            delegate_targets++;
            func();
            if (C) C->send_completion(origin);
          });
        }
      }

      // async call with return val (via Promise)
      template< typename T >
      static void call(Core dest, F f, T (F::*mf)() const) {
        static_assert(std::is_same<void,T>::value, "not implemented yet");
        // return std::move(Promise<T>(f()));
        //impl::call(dest, f, mf);
      }

    };

    template< GlobalCompletionEvent * C, typename F >
    struct Specializer<SyncMode::Blocking,C,F> {
      template< typename T >
      static auto call(Core dest, F f, T (F::*mf)() const) -> T {
        return impl::call(dest, f, mf); // defined in DelegateBase.hpp
      }
    };

  } // namespace impl

  namespace delegate {

#ifdef GRAPPA_CACHE_ENABLE
    static void verify_cache(const impl::cache_info& mycache) {
      while (mycache.refcnt > 0)
        // Grappa::yield cannot be called in Addressing.hpp
        Grappa::yield();
    }
#endif

#ifdef GRAPPA_WI_CACHE
    enum InvResponse { Miss, Retry, Succ }; 
#endif

#ifdef GRAPPA_TARDIS_CACHE
    enum CacheState { Owner, Hit, Expired, Miss };

    static CacheState try_read_cache(const impl::cache_info& mycache,
        bool valid) {
      if (!valid) {
        delegate_cache_miss++;
        return CacheState::Miss;
      }
      if (Grappa::mypts() > mycache.rts) {
        delegate_cache_expired++;
        return CacheState::Expired;
      }
      delegate_cache_hit++;
      return CacheState::Hit;
    }
#endif // GRAPPA_TARDIS_CACHE

#define AUTO_INVOKE(expr) decltype(expr) { return expr; }

    template< SyncMode S = SyncMode::Blocking,
              GlobalCompletionEvent * C = &impl::local_gce,
              typename F = decltype(nullptr) >
    auto internal_call(Core dest, F f) -> AUTO_INVOKE((impl::call(dest, f,
            &F::operator())));

    template< SyncMode S = SyncMode::Blocking,
              GlobalCompletionEvent * C = &impl::local_gce,
              typename F = decltype(nullptr) >
    auto call(Core dest, F f) -> decltype((impl::Specializer<S,C,F>::call(dest, f,
            &F::operator()))) {
      delegate_call_all++;
      return internal_call(dest, f);
    }

  } // namespace delegate

  namespace impl {
    template< SyncMode S, GlobalCompletionEvent * C, typename T, typename R, typename F >
    inline auto call(GlobalAddress<T> t, F func, R (F::*mf)(T&) const) ->
      decltype(func(*t.pointer())) {
      delegate_call_one++;
      return delegate::internal_call<S,C>(t.core(), [t,func] {
        return func(*t.pointer());
      });
    }
    template< SyncMode S, GlobalCompletionEvent * C, typename T, typename R, typename F >
    inline auto call(GlobalAddress<T> t, F func, R (F::*mf)(T*) const) ->
      decltype(func(t.pointer())) {
      delegate_call_one++;
      return delegate::internal_call<S,C>(t.core(), [t,func] {
        return func(t.pointer());
      });
    }
  }

  namespace delegate {

    /// Helper that makes it easier to implement custom delegate operations
    /// specifically on global addresses.
    ///
    /// Does specialization based on return type of the lambda.
    ///
    /// Example:
    /// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    /// GlobalAddress<int> xa;
    /// bool is_zero = delegate::call(xa, [](int* x){ return *x == 0; });
    ///
    /// // or by reference:
    /// bool is_zero = delegate::call(xa, [](int& x){ return x == 0; });
    /// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    template< SyncMode S = SyncMode::Blocking,
              GlobalCompletionEvent * C = &impl::local_gce,
              typename T = decltype(nullptr),
              typename F = decltype(nullptr) >
    inline auto call(GlobalAddress<T> t, F func) ->
    AUTO_INVOKE((impl::call<S,C>(t,func,&F::operator())));

#undef AUTO_INVOKE

    /// Try lock on remote mutex. Does \b not lock or unlock, creates a SuspendedDelegate if lock has already
    /// been taken, which is triggered on unlocking of the Mutex.
    template< typename M, typename F >
    inline auto call(Core dest, M mutex, F func) -> decltype(func(mutex())) {
      CHECK(0);
      using R = decltype(func(mutex()));
      delegate_ops++;

      if (dest == mycore()) {
        delegate_targets++;
        delegate_short_circuits++;
        // auto l = mutex();
        // lock(l);
        auto r = func(mutex());
        // unlock(l);
        return r;
      } else {
        FullEmpty<R> result;
        auto result_addr = make_global(&result);
        auto set_result = [result_addr](const R& val){
          send_heap_message(result_addr.core(), [result_addr,val]{
            result_addr->writeXF(val);
          });
        };

        send_message(dest, [set_result,mutex,func] {
          delegate_targets++;
          auto l = mutex();
          if (is_unlocked(l)) { // if lock is not held
            // lock(l);
            set_result(func(l));
          } else {
            add_waiter(l, SuspendedDelegate::create([set_result,func,l]{
              // lock(l);
              CHECK(is_unlocked(l));
              set_result(func(l));
            }));
          }
        });
        auto r = result.readFE();
        return r;
      }
    }

    /// Alternative version of delegate::call that spawns a privateTask to allow the delegate
    /// to perform suspending actions.
    ///
    /// @note Use of this is not advised: suspending violates much of the assumptions about
    /// delegates we usually make, and can easily cause deadlock if no workers are available
    /// to execute the spawned privateTask. A better option for possibly-blocking delegates
    /// is to use the Mutex version of delegate::call(Core,M,F).
    template <typename F>
    inline auto call_suspendable(Core dest, F func) -> decltype(func()) {
      CHECK(0);
      delegate_ops++;
      using R = decltype(func());
      Core origin = Grappa::mycore();

      if (dest == origin) {
        delegate_targets++;
        delegate_short_circuits++;
        return func();
      } else {
        FullEmpty<R> result;
        int64_t network_time = 0;
        int64_t start_time = Grappa::timestamp();

        send_message(dest, [&result, origin, func, &network_time, start_time] {
          delegate_targets++;

          spawn([&result, origin, func, &network_time, start_time] {
            R val = func();
            // TODO: replace with handler-safe send_message
            send_heap_message(origin, [&result, val, &network_time, start_time] {
              network_time = Grappa::timestamp();
              record_network_latency(start_time);
              result.writeXF(val); // can't block in message, assumption is that result is already empty
            });
          });
        }); // send message
        // ... and wait for the result
        R r = result.readFE();
        record_wakeup_latency(start_time, network_time);
        return r;
      }
    }

    /// Read the value (potentially remote) at the given GlobalAddress, blocks the calling task until
    /// round-trip communication is complete.
    /// @warning Target object must lie on a single node (not span blocks in global address space).
    template< SyncMode S = SyncMode::Blocking,
              CacheMode M = CacheMode::WriteBack,
              GlobalCompletionEvent * C = &impl::local_gce,
              typename T = decltype(nullptr) >
    T read(GlobalAddress<T> target) {
      delegate_reads++;

#ifdef GRAPPA_CACHE_ENABLE
      if (M == CacheMode::WriteThrough) {
        return internal_call<S,C>(target.core(), [target]() -> T {
          delegate_read_targets++;
          return *target.pointer();
        });
      }
#endif // GRAPPA_CACHE_ENABLE

#ifdef GRAPPA_TARDIS_CACHE
      if (target.is_owner()) {
        auto& owner_ts = GlobalAddress<T>::find_owner_info(target);

        /*LOG(INFO) << "Core " << Grappa::mycore() << " ptr " << target.raw_bits()
          << " #" << delegate_reads << " read wts " << owner_ts.wts << " rts "
          << owner_ts.rts << " pts " << Grappa::mypts() << " owner:" << target.core();*/
        if (owner_ts.rts < Grappa::mypts()) {
          owner_ts.rts = Grappa::mypts();
        }
        return *target.pointer();
      }

      bool valid;
      impl::cache_info& mycache = GlobalAddress<T>::find_cache(target, &valid);
      verify_cache(mycache);
      GlobalAddress<T>::active_cache(mycache);
      if (try_read_cache(mycache, valid) == CacheState::Hit) {
        /*LOG(INFO) << "Core " << Grappa::mycore() << " ptr " << target.raw_bits()
          << " #" << delegate_reads << " read wts "
          << mycache.wts << " rts " << mycache.rts << " pts " << Grappa::mypts() << " owner:" << target.core();*/
        GlobalAddress<T>::deactive_cache(mycache);
        return *(T*)mycache.get_object();
      }

      timestamp_t pts = Grappa::mypts();
      timestamp_t wts = mycache.wts;

      // Expired: try to renew first
      if (valid) {
        auto r = internal_call<S,C>(target.core(), [target, pts, wts]() {
          delegate_read_targets++;

          auto& owner_ts = GlobalAddress<T>::find_owner_info(target);
          if (owner_ts.wts == wts) {
            owner_ts.rts = std::max<timestamp_t>(std::max<timestamp_t>(
                  owner_ts.rts, owner_ts.wts + LEASE), (pts + LEASE));
            return owner_ts.rts;
          }
          else {
            return (timestamp_t)~0L;
          }
        });
        if (r != (timestamp_t)~0L) {
          delegate_fast_path++;
          mycache.rts = r;
          GlobalAddress<T>::deactive_cache(mycache);
          return *(T*)mycache.get_object();
        }
      }

      // Ask for the latest object.
      auto r = internal_call<S,C>(target.core(), [target, pts]() {
        delegate_read_targets++;

        auto& owner_ts = GlobalAddress<T>::find_owner_info(target);
        owner_ts.rts = std::max<timestamp_t>(std::max<timestamp_t>(
              owner_ts.rts, owner_ts.wts + LEASE), (pts + LEASE));
        return impl::rpc_read_result<T>(*target.pointer(), owner_ts);
      });
      mycache.assign(&r.r);
      mycache.rts = r.rts;
      mycache.wts = r.wts;
      Grappa::mypts() = std::max<timestamp_t>(pts, r.wts);
      /*LOG(INFO) << "Core(M||E) " << Grappa::mycore() << " ptr " << target.raw_bits()
        << " #" << delegate_reads << " read wts "
        << mycache.wts << " rts " << mycache.rts << " pts " << Grappa::mypts();*/
      GlobalAddress<T>::deactive_cache(mycache);
      return r.r;
#elif (defined(GRAPPA_WI_CACHE) || defined(GRAPPA_WB_CACHE))
      if (target.is_owner()) {
        auto& owner_ts = GlobalAddress<T>::find_owner_info(target);
        while (owner_ts.locked) {
          Grappa::yield();
        }
        return *target.pointer();
      }

      bool valid;
      impl::cache_info& mycache = GlobalAddress<T>::find_cache(target, &valid);
      verify_cache(mycache);
      GlobalAddress<T>::active_cache(mycache);
      if (valid && mycache.valid) {
        /*LOG(ERROR) << "Core " << Grappa::mycore() << " ptr " << target.raw_bits()
          << " read hits.";*/
        delegate_cache_hit++;
        GlobalAddress<T>::deactive_cache(mycache);
        return *(T*)mycache.get_object();
      }
      /*LOG(ERROR) << "Core " << Grappa::mycore() << " ptr " << target.raw_bits()
        << " read misses";*/
      delegate_cache_miss++;

      Core my = Grappa::mycore();

retry:
      auto r = internal_call<S,C>(target.core(), [my, target]() {
        delegate_read_targets++;

        auto& info = GlobalAddress<T>::find_owner_info(target);
        if (!info.locked) {
          info.copyset[my] = true;
        }
        return lock_obj<T>{ *target.pointer(), info.locked };
      });
      if (r.locked)
        goto retry;

      mycache.valid = true;
      mycache.assign(&r.object);
      GlobalAddress<T>::deactive_cache(mycache);
      return r.object;
#else
      return internal_call<S,C>(target.core(), [target]() -> T {
        delegate_read_targets++;
        return *target.pointer();
      });
#endif
    }

    /// Remove 'const' qualifier to do read.
    template< SyncMode S = SyncMode::Blocking,
              CacheMode M = CacheMode::WriteBack,
              GlobalCompletionEvent * C = &impl::local_gce,
              typename T = decltype(nullptr) >
    T read(GlobalAddress<const T> target) {
      return read<S,M,C>(static_cast<GlobalAddress<T>>(target));
    }

    /// Blocking remote write.
    /// @warning Target object must lie on a single node (not span blocks in global address space).
    template< SyncMode S = SyncMode::Blocking,
              CacheMode M = CacheMode::WriteBack,
              GlobalCompletionEvent * C = &impl::local_gce,
              typename T = decltype(nullptr),
              typename U = decltype(nullptr) >
    void write(GlobalAddress<T> target, U value) {
      static_assert(std::is_convertible<T,U>(), "type of value must match GlobalAddress type");
      delegate_writes++;
#ifdef GRAPPA_CACHE_ENABLE
      if (M == CacheMode::WriteThrough) {
        internal_call<S,C>(target.core(), [target, value]() -> T {
          delegate_write_targets++;
          *target.pointer() = value;
        });
      }
#endif // GRAPPA_CACHE_ENABLE

#ifdef GRAPPA_TARDIS_CACHE
      if (target.is_owner()) {
        auto& owner_ts = GlobalAddress<T>::find_owner_info(target);
        timestamp_t ts = std::max<timestamp_t>(Grappa::mypts(), owner_ts.rts + 1);
        Grappa::mypts() = owner_ts.rts = owner_ts.wts = ts;
        /// Update owner storage.
        *target.pointer() = value;
        /*LOG(INFO) << "Core " << Grappa::mycore() << "(O) ptr " << target.raw_bits()
          << " #" << delegate_writes << " write wts "
          << owner_ts.wts << " rts " << owner_ts.rts << " pts " << Grappa::mypts();*/
        return;
      }

      impl::cache_info& mycache = GlobalAddress<T>::find_cache(target);
      verify_cache(mycache);
      GlobalAddress<T>::active_cache(mycache);
      auto r = internal_call<S,C>(target.core(), [target, value] {
        delegate_write_targets++;

        auto& owner_ts = GlobalAddress<T>::find_owner_info(target);
        timestamp_t ts = std::max<timestamp_t>(Grappa::mypts(), owner_ts.rts + 1);
        Grappa::mypts() = owner_ts.wts = owner_ts.rts = ts;
        *target.pointer() = value;
        return ts;
      });
      Grappa::mypts() = mycache.rts = mycache.wts =
        std::max<timestamp_t>(Grappa::mypts(), r);
      mycache.assign(&value);
      /*LOG(INFO) << "Core " << Grappa::mycore() << " ptr " << target.raw_bits()
        << " #" << delegate_writes << " write wts "
        << mycache.wts << " rts " << mycache.rts << " pts " << Grappa::mypts();*/
      GlobalAddress<T>::deactive_cache(mycache);
#elif defined(GRAPPA_WI_CACHE)
      if (target.is_owner()) {
        auto& info = GlobalAddress<T>::find_owner_info(target);

        while (info.locked) {
          Grappa::yield();
        }
        /*LOG(ERROR) << "Core " << Grappa::mycore() << " ptr " << target.raw_bits()
          << " write as owner.";*/
        info.locked = true;

        int cnt = 0;
        // Broadcast invalidation messages according to the copyset.
        for (int i = 0; i < info.copyset.size(); i++) {
          if (info.copyset[i] && Grappa::mycore() != i) {
            auto r = internal_call<S,C>((Core)i, [target] {
              bool valid;
              auto& mycache = GlobalAddress<T>::find_cache(target, &valid, false);
              if (!valid) {
                return InvResponse::Miss;
              }
              else {
                mycache.valid = false;
                return InvResponse::Succ;
              }
            });
            /*LOG(ERROR) << "Core " << Grappa::mycore() << " ptr " << target.raw_bits()
              << " sends INV as owner to Core " << i << " with result " << r;*/
            if (r == InvResponse::Miss) info.copyset[i] = false;
          }
        }

        info.locked = false;
        *target.pointer() = value;
      }

      impl::cache_info& mycache = GlobalAddress<T>::find_cache(target);
      verify_cache(mycache);
      GlobalAddress<T>::active_cache(mycache);

      /*LOG(ERROR) << "Core " << Grappa::mycore() << " ptr " << target.raw_bits()
        << " write as non-owner.";*/
      // Embedded delegataions are disallowed in Grappa.
      // Lock this object.
      auto cpyset = internal_call<S,C>(target.core(), [target] {
        auto& info = GlobalAddress<T>::find_owner_info(target);
        info.locked = true;
        return info.copyset;
      });
      // Broadcast invalidation messages according to the copyset.
      for (int i = 0; i < cpyset.size(); i++) {
        if (cpyset[i] && i != Grappa::mycore()) {
          auto r = internal_call<S,C>((Core)i, [target, value] {
            bool valid;
            auto& mycache = GlobalAddress<T>::find_cache(target, &valid, false);
            if (!valid) {
              return false;
            }
            else {
              mycache.valid = false;
              return true;
            }
          });
          /*LOG(ERROR) << "Core " << Grappa::mycore() << " ptr " << target.raw_bits()
            << " sends INV as non-owner to Core " << i << " with result " << r;*/
          if (!r) cpyset[i] = false;
        }
      }
      cpyset[Grappa::mycore()] = true;
      // Unlock this object and update the copyset.
      internal_call<S,C>(target.core(), [target, value, cpyset] {
        delegate_write_targets++;

        auto& info = GlobalAddress<T>::find_owner_info(target);
        info.copyset = cpyset;
        info.locked = false;
        *target.pointer() = value;
      });
      mycache.valid = true;
      mycache.assign(&value);
      GlobalAddress<T>::deactive_cache(mycache);
#elif defined(GRAPPA_WB_CACHE)
#else // Vanilla Grappa
      internal_call<S,C>(target.core(), [target, value]() -> T {
        delegate_write_targets++;
        *target.pointer() = value;
      });
#endif
    }

    /// Fetch the value at `target`, increment the value stored there with `inc` and return the
    /// original value to blocking thread.
    /// @warning Target object must lie on a single node (not span blocks in global address space).
    template< SyncMode S = SyncMode::Blocking,
              GlobalCompletionEvent * C = &impl::local_gce,
              typename T = decltype(nullptr),
              typename U = decltype(nullptr) >
    T fetch_and_add(GlobalAddress<T> target, U inc) {
      delegate_fetchadds++;
      return internal_call(target.core(), [target, inc]() -> T {
        delegate_fetchadd_targets++;
        T* p = target.pointer();
        T r = *p;
        *p += inc;
        return r;
      });
    }

    /// Flat combines fetch_and_add to a single global address
    /// @warning Target object must lie on a single node (not span blocks in global address space).
    template < typename T, typename U >
    class FetchAddCombiner {
      // TODO: generalize to define other types of combiners
      private:
        // configuration
        const GlobalAddress<T> target;
        const U initVal;
        const uint64_t flush_threshold;

        // state
        T result;
        U increment;
        uint64_t committed;
        uint64_t participant_count;
        uint64_t ready_waiters;
        bool outstanding;
        ConditionVariable untilNotOutstanding;
        ConditionVariable untilReceived;

        // wait until fetch add unit is in aggregate mode
        // TODO: add concurrency (multiple fetch add units)
        void block_until_ready() {
          while ( outstanding ) {
            ready_waiters++;
            Grappa::wait(&untilNotOutstanding);
            ready_waiters--;
          }
        }

        void set_ready() {
          outstanding = false;
          Grappa::broadcast(&untilNotOutstanding);
        }

        void set_not_ready() {
          outstanding = true;
        }

      public:
        FetchAddCombiner( GlobalAddress<T> target, uint64_t flush_threshold, U initVal )
         : target( target )
         , initVal( initVal )
         , flush_threshold( flush_threshold )
         , result()
         , increment( initVal )
         , committed( 0 )
         , participant_count( 0 )
         , ready_waiters( 0 )
         , outstanding( false )
         , untilNotOutstanding()
         , untilReceived()
         {}

        /// Promise that in the future
        /// you will call `fetch_and_add`.
        ///
        /// Must be called before a call to `fetch_and_add`
        ///
        /// After calling promise, this task must NOT have a dependence on any
        /// `fetch_and_add` occurring before it calls `fetch_and_add` itself
        /// or deadlock may occur.
        ///
        /// For good performance, should allow other
        /// tasks to run before calling `fetch_and_add`
        void promise() {
          committed += 1;
        }
        // because tasks run serially, promise() replaces the flat combining tree

        T fetch_and_add( U inc ) {

          block_until_ready();

          // fetch add unit is now aggregating so add my inc

          participant_count++;
          committed--;
          increment += inc;

          // if I'm the last entered client and either the flush threshold
          // is reached or there are no more committed participants then start the flush
          if ( ready_waiters == 0 && (participant_count >= flush_threshold || committed == 0 )) {
            set_not_ready();
            uint64_t increment_total = increment;
            flat_combiner_fetch_and_add_amount += increment_total;
            auto t = target;
            result = internal_call(target.core(), [t, increment_total]() -> U {
              T * p = t.pointer();
              uint64_t r = *p;
              *p += increment_total;
              return r;
            });
            // tell the others that the result has arrived
            Grappa::broadcast(&untilReceived);
          } else {
            // someone else will start the flush
            Grappa::wait(&untilReceived);
          }

          uint64_t my_start = result;
          result += inc;
          participant_count--;
          increment -= inc;   // for validation purposes (could just set to 0)
          if ( participant_count == 0 ) {
            CHECK( increment == 0 ) << "increment = " << increment << " even though all participants are done";
            set_ready();
          }

          return my_start;
        }
    };


    /// If value at `target` equals `cmp_val`, set the value to `new_val` and return `true`,
    /// otherwise do nothing and return `false`.
    /// @warning Target object must lie on a single node (not span blocks in global address space).
    template< SyncMode S = SyncMode::Blocking,
              GlobalCompletionEvent * C = &impl::local_gce,
              typename T = decltype(nullptr),
              typename U = decltype(nullptr),
              typename V = decltype(nullptr) >
    bool compare_and_swap(GlobalAddress<T> target, U cmp_val, V new_val) {
      static_assert(std::is_convertible<T,U>(), "type of cmp_val must match GlobalAddress type");
      static_assert(std::is_convertible<T,V>(), "type of new_val must match GlobalAddress type");

      delegate_cmpswaps++;
      return internal_call(target.core(), [target, cmp_val, new_val]() -> bool {
        T * p = target.pointer();
        delegate_cmpswap_targets++;
        if (cmp_val == *p) {
          *p = new_val;
          return true;
        } else {
          return false;
        }
      });
    }

    template< SyncMode S = SyncMode::Blocking,
              GlobalCompletionEvent * C = &impl::local_gce,
              typename T = decltype(nullptr),
              typename U = decltype(nullptr) >
    void increment(GlobalAddress<T> target, U inc) {
      static_assert(std::is_convertible<T,U>(), "type of inc must match GlobalAddress type");
      delegate_async_increments++;
      delegate::call<SyncMode::Async,C>(target.core(), [target,inc]{
        (*target.pointer()) += inc;
      });
    }

  } // namespace delegate

  /// Synchronizing remote private task spawn. Automatically enrolls task with GlobalCompletionEvent and
  /// sends `complete`  message when done (if C is non-null).
  template< TaskMode B = TaskMode::Bound,
            GlobalCompletionEvent * C = &impl::local_gce,
            typename F = decltype(nullptr) >
  void spawnRemote(Core dest, F f) {
    if (C) C->enroll();
    Core origin = mycore();
    delegate::call<SyncMode::Async,nullptr>(dest, [origin,f] {
      spawn<B>([origin,f] {
        f();
        if (C) C->send_completion(origin);
      });
    });
  }

  // overload to specify just the GCE
  template< GlobalCompletionEvent * C,
            TaskMode B = TaskMode::Bound,
            typename F = decltype(nullptr) >
  void spawnRemote(Core dest, F f) {
    spawnRemote<B, C, F>( dest, f );
  }

} // namespace Grappa
/// @}


