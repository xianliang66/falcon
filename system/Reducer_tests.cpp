////////////////////////////////////////////////////////////////////////
// This file is part of Grappa, a system for scaling irregular
// applications on commodity clusters. 

// Copyright (C) 2010-2014 University of Washington and Battelle
// Memorial Institute. University of Washington authorizes use of this
// Grappa software.

// Grappa is free software: you can redistribute it and/or modify it
// under the terms of the Affero General Public License as published
// by Affero, Inc., either version 1 of the License, or (at your
// option) any later version.

// Grappa is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// Affero General Public License for more details.

// You should have received a copy of the Affero General Public
// License along with this program. If not, you may obtain one from
// http://www.affero.org/oagpl.html.
////////////////////////////////////////////////////////////////////////


#include <boost/test/unit_test.hpp>
#include "Grappa.hpp"
#include "GlobalAllocator.hpp"
#include "Reducer.hpp"
#include "Barrier.hpp"

BOOST_AUTO_TEST_SUITE( Reducer_tests );

using namespace Grappa;
//#define TEST_MSG BOOST_MESSAGE( "In test " << __func__ );
#define TEST(name) void test_##name()
#define RUNTEST(name) BOOST_MESSAGE( "== Test " << #name << " =="); test_##name()

TEST(int_add) {
  int64_t expected = Grappa::cores(); 
  on_all_cores( [expected] {
    AllReducer<int64_t, collective_add> r( 0 );
    r.reset();
    r.accumulate( 1 );

    barrier();

    int64_t result = r.finish();
    BOOST_CHECK( result == expected );
  });
}

TEST(int_add_more) {
  int64_t nc = Grappa::cores();
  int64_t expected = nc*(nc+1)*(2*nc+1)/6;
  on_all_cores( [expected] {
    AllReducer<int64_t, collective_add> r( 0 );
    r.reset();
    for (int i=0; i<Grappa::mycore()+1; i++) {
      r.accumulate( Grappa::mycore()+1 );
    }

    barrier();

    int64_t result = r.finish();
    BOOST_CHECK( result == expected );
  });
}

TEST(int_max) {
  int64_t expected = 1; 
  on_all_cores( [expected] {
    AllReducer<int64_t, collective_max> r( 0 );
    r.reset();
    r.accumulate( -1*Grappa::mycore() + 1  );

    barrier();

    int64_t result = r.finish();
    BOOST_CHECK( result == expected );
  });
}
  
SimpleSymmetric<bool> s_active;
SimpleSymmetric<int> s_count;

Reducer<int,ReducerType::Add> count;
Reducer<bool,ReducerType::Or> active;

BOOST_AUTO_TEST_CASE( test1 ) {
  Grappa::init( GRAPPA_TEST_ARGS );
  Grappa::run([]{
    BOOST_CHECK(Grappa::cores() >= 2); // at least 2 nodes for these tests...

    RUNTEST(int_add);
    RUNTEST(int_add_more);
    RUNTEST(int_max);
    
    BOOST_MESSAGE("== Test SimpleSymmetric<T> ==");
    set(s_active, false);
    BOOST_CHECK_EQUAL(any(s_active), false);
    
    s_active |= true;
    BOOST_CHECK_EQUAL(any(s_active), true);
    BOOST_CHECK_EQUAL(all(s_active), false);
    
    set(s_count, 0);
    BOOST_CHECK_EQUAL(sum(s_count), 0);
    
    s_count += 1;
    BOOST_CHECK_EQUAL(sum(s_count), 1);
    
    call_on_all_cores([]{ s_count += 1; });
    BOOST_CHECK_EQUAL(sum(s_count), cores()+1);
    
    BOOST_MESSAGE("== Test Reducer<T> ==");
    
    BOOST_CHECK_EQUAL(count, 0);
    
    count++;
    BOOST_CHECK_EQUAL(count, 1);
    
    on_all_cores([]{ count++; });
    BOOST_CHECK_EQUAL(count, cores()+1);
    
    BOOST_CHECK(!active);
    
    active |= true;
    BOOST_CHECK(active);
    
    active = false;
    BOOST_CHECK(!active);
    
  });
  Grappa::finalize();
}

BOOST_AUTO_TEST_SUITE_END();
