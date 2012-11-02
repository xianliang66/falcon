// Copyright 2010-2012 University of Washington. All Rights Reserved.
// LICENSE_PLACEHOLDER
// This software was created with Government support under DE
// AC05-76RL01830 awarded by the United States Department of
// Energy. The Government has certain rights in the software.


#include <boost/test/unit_test.hpp>

#include "Grappa.hpp"
#include "Collective.hpp"
#include "ForkJoin.hpp"

// Tests the functions in Collective.hpp
// FIXME: remove tests for deprecated Grappa_collective_reduce

BOOST_AUTO_TEST_SUITE( Collective_tests );

struct worker_args {
    int64_t add1Operand;
    int64_t max1Operand;

    int64_t add1Result;
    int64_t max1Result;
};

worker_args this_node_wargs;
const int64_t add_init = 0;
const int64_t max_init = -1000;

void worker_thread_f(Thread* me, void* args) {
    worker_args* wargs = (worker_args*) args;

    BOOST_MESSAGE( "worker " << Grappa_mynode() << " entering add reduce" );
    wargs->add1Result = Grappa_collective_reduce(COLL_ADD, 0, wargs->add1Operand, add_init);
    BOOST_MESSAGE( "worker " << Grappa_mynode() << " entering max reduce" );
    wargs->max1Result = Grappa_collective_reduce(COLL_MAX, 0, wargs->max1Operand, max_init);
    
    BOOST_MESSAGE( "worker " << Grappa_mynode() << " is finished" );
    Grappa_barrier_commsafe();
    BOOST_MESSAGE( "worker " << Grappa_mynode() << " exits finished barrier" );
}

void spawn_worker_am( worker_args* args, size_t size, void* payload, size_t payload_size ) {
   /* in general (for async am handling) this may need synchronization */
   memcpy(&this_node_wargs, args, size);
   BOOST_MESSAGE( "Remote is spawning worker " << Grappa_mynode() );
   Grappa_spawn(&worker_thread_f, &this_node_wargs); 
}

LOOP_FUNCTION( all_reduce_test_func, nid ) {
  int64_t myval = 123;
  
  int64_t sum = Grappa_allreduce<int64_t,coll_add<int64_t>,0>(myval);
  BOOST_CHECK_EQUAL(sum, 123*Grappa_nodes());
}

void user_main( int * args ) 
{
    BOOST_MESSAGE( "Spawning user main Thread " << (void *) CURRENT_THREAD <<
            " " << CURRENT_THREAD <<
            " on node " << Grappa_mynode() );

    Node expectedNodes = 4; 
    BOOST_CHECK_EQUAL( expectedNodes, Grappa_nodes() );

    // spawn worker threads 
    Thread* worker_thread;
    worker_args wargss[expectedNodes];
    for (int nod = 0; nod<expectedNodes; nod++) {
        wargss[nod].add1Operand = nod-1;
        wargss[nod].max1Operand = -nod;
        if (nod == Grappa_mynode()) {
            BOOST_MESSAGE( "Spawing worker " << nod );
            worker_thread = Grappa_spawn( &worker_thread_f, &wargss[nod] );
        } else {
            BOOST_MESSAGE( "Spawing worker " << nod );
            Grappa_call_on( nod, &spawn_worker_am, &wargss[nod] );
        }
    }
    BOOST_MESSAGE( "user_main joins" );
    Grappa_join(worker_thread);//because 0 leads reduction this join will mean everything is done
    BOOST_MESSAGE( "user_main leaves join" );

    // calculate the reductions manually
    int64_t addres = add_init;
    int64_t maxres = max_init;
    for (int nod = 0; nod < expectedNodes; nod++) {
        addres += wargss[nod].add1Operand; 
        maxres = (wargss[nod].max1Operand > maxres) ? 
                        wargss[nod].max1Operand 
                      : maxres;
    }
    BOOST_CHECK_EQUAL ( addres, wargss[0].add1Result );
    BOOST_CHECK_EQUAL ( maxres, wargss[0].max1Result );

  all_reduce_test_func f;
  fork_join_custom(&f);

    Grappa_signal_done();
}

BOOST_AUTO_TEST_CASE( test1 ) {

  Grappa_init( &(boost::unit_test::framework::master_test_suite().argc),
                &(boost::unit_test::framework::master_test_suite().argv) );

  Grappa_activate();

  Grappa_run_user_main( &user_main, (int*)NULL );
  BOOST_CHECK( Grappa_done() == true );

  Grappa_finish( 0 );
}

BOOST_AUTO_TEST_SUITE_END();

