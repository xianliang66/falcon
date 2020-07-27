#include <Grappa.hpp>
#include <tasks/DictOut.hpp>
#include <Reducer.hpp>
#include <graph/Graph.hpp>

#include <iostream>

using namespace Grappa;

DEFINE_bool(metrics, false, "Dump metrics to stdout.");

// input size
DEFINE_int32( scale, 18, "Log2 number of vertices." );
DEFINE_int32(edgefactor, 12, "Average number of edges per vertex.");

// input file
DEFINE_string(path, "", "Path to graph source file");
DEFINE_string(format, "bintsv4", "Format of graph source file");

// pagerank options
DEFINE_double( damping, 0.8f, "Pagerank damping factor" );
DEFINE_double( epsilon, 0.001f, "Acceptable error magnitude" );

// runtime statistics
GRAPPA_DEFINE_METRIC(SummarizingMetric<double>, iterations_time, 0); // provides total time, avg iteration time, number of iterations
GRAPPA_DEFINE_METRIC(SimpleMetric<double>, init_pagerank_time, 0);
GRAPPA_DEFINE_METRIC(SimpleMetric<double>, multiply_time, 0);
GRAPPA_DEFINE_METRIC(SimpleMetric<double>, vector_add_time, 0);
GRAPPA_DEFINE_METRIC(SimpleMetric<double>, norm_and_diff_time, 0);

// output statistics (ensure that only core 0 sets this exactly once AFTER `reset` and before `merge_and_print`)
GRAPPA_DEFINE_METRIC(SimpleMetric<double>, pagerank_time, 0);
GRAPPA_DEFINE_METRIC(SimpleMetric<double>, make_graph_time, 0);
GRAPPA_DEFINE_METRIC(SimpleMetric<double>, tuples_to_csr_time, 0);
GRAPPA_DEFINE_METRIC(SimpleMetric<uint64_t>, actual_nnz, 0);

struct PagerankVertex {
  size_t nadj;
  double weight;

  PagerankVertex() : nadj(0), weight(0) {}
};

enum thread_state { INIT = 0x0, RUNNING = 0xBA, UPDATE = 0xCA, TERMINATE = 0xFF };

static bool terminated(GlobalAddress<thread_state> complete_addr) {
  if (delegate::read(complete_addr) != INIT) {
    on_all_cores([complete_addr] {
      for (int i = 0; i < Grappa::cores(); i++) {
        while (delegate::read(complete_addr + i) == RUNNING) {
          Grappa::yield();
#ifdef GRAPPA_TARDIS_CACHE
          Grappa::mypts() += 10;
#endif
        }
      }
    });
  }
  for (int i = 0; i < Grappa::cores(); i++) {
    thread_state ts = delegate::read(complete_addr + i);
    if (ts == UPDATE || ts == INIT) {
      for (int j = 0; j < Grappa::cores(); j++) {
        delegate::write(complete_addr + j, RUNNING);
      }
      return false;
    }
  }
  return true;
}

static GlobalCompletionEvent gce;
static int nupdates = 0;
// Iterative method
// R(t+1) = dMR(t) + (1-d)/N vec(1)
void pagerank( GlobalAddress<Graph<PagerankVertex>> g,
    double damp_factor, double epsilon ) {
    // expose global completion flag to global address space
    GlobalAddress<thread_state> complete_addr =
      global_alloc<thread_state>(Grappa::cores());

    int iter = 0;
    static bool local_complete = true;
    while (!terminated(complete_addr)) {
      LOG(ERROR) << "iteration --> " << iter++;

      // iterate over all vertices of the graph
      on_all_cores([g,complete_addr,damp_factor,epsilon]{
        // Remote call async
        local_complete = true;
        for (VertexID id = 0; id < g->nv; id++) {
          if ((g->vs+id).core() == Grappa::mycore()) {
            auto func = [g,id,damp_factor,epsilon] {
              auto v = delegate::read(g->vs+id);
              double prev_val = v.data.weight;
              double new_val = 0.0;
              std::vector <VertexID> adjs = g->get_adj(id);
              for (auto iter = adjs.begin(); iter != adjs.end(); iter++) {
                auto adj = delegate::read(g->vs+*iter);
                new_val += adj.data.weight / adj.data.nadj;
              }
              new_val *= damp_factor;
              if (std::abs(new_val - prev_val) > epsilon) {
                v.data.weight = new_val;
                delegate::write(g->vs+id, v);
                local_complete = false;
                nupdates++;
              }
            };
            spawnRemote<&gce>(Grappa::mycore(), func);
            //func();
        }
      }
      gce.wait();
      LOG(ERROR) << "Core " << Grappa::mycore() << " updates " << nupdates
        << " vertices.";
      nupdates = 0;
      if (local_complete) {
        delegate::write(complete_addr + Grappa::mycore(), TERMINATE);
      }
      else {
        delegate::write(complete_addr + Grappa::mycore(), UPDATE);
      }
    });
  }
}

int main(int argc, char* argv[]) {
  Grappa::init(&argc, &argv);
  Grappa::run([]{
    // to be assigned to stats output
    double pagerank_time_SO;
    uint64_t N = (1L << FLAGS_scale);
    int64_t NE = N * FLAGS_edgefactor;

    long userseed = 0xDECAFBAD; // from (prng.c: default seed)

    TupleGraph tg;

    if( FLAGS_path.empty() ) {
      tg = TupleGraph::Kronecker(FLAGS_scale, NE, userseed, userseed);
    } else {
      // load from file
      tg = TupleGraph::Load( FLAGS_path, FLAGS_format );
    }

    auto g = Graph<PagerankVertex>::create(tg);

    // Initalize vertex data
    on_all_cores([g]{
      for (VertexID id = 0; id < g->nv; id++) {
        if ((g->vs+id).core() == Grappa::mycore()) {
          auto func = [g,id] {
            auto v = delegate::read(g->vs+id);
            std::vector <VertexID> adjs = g->get_adj(id);
            v.data.weight = 1.0;
            v.data.nadj = adjs.size();
            delegate::write(g->vs+id, v);
          };
          spawnRemote<&gce>(Grappa::mycore(), func);
        }
      }
      gce.wait();
    });

    Metrics::reset();
    Metrics::start_tracing();

    pagerank_time_SO = walltime();

    pagerank( g, FLAGS_damping, FLAGS_epsilon );

    pagerank_time_SO = (walltime() - pagerank_time_SO);

    LOG(ERROR) << "time=" << pagerank_time_SO << " proto:"
      << GRAPPA_CC_PROTOCOL_NAME;

    Metrics::stop_tracing();

    Metrics::merge_and_dump_to_file();

    g->destroy();
  });
  Grappa::finalize();
}
