#include <Grappa.hpp>
#include <GlobalVector.hpp>
#include <graph/Graph.hpp>

#include "pagerank.hpp"

#define NO_TEST 3
/* Options */
DEFINE_bool(metrics, false, "Dump metrics");
DEFINE_int32(scale, 23, "Log2 number of vertices.");
DEFINE_int32(edgefactor, 24, "Average number of edges per vertex.");

// pagerank options
DEFINE_double( damping, 0.8f, "Pagerank damping factor" );
DEFINE_double( epsilon, 0.001f, "Acceptable error magnitude" );

using namespace Grappa;

GRAPPA_DEFINE_METRIC(SummarizingMetric<double>, pagerank_time, 0);
GRAPPA_DEFINE_METRIC(SimpleMetric<double>, graph_create_time, 0);

static uint32_t nupdates = 0;

void reset_pagerank(GlobalAddress<G>& g) {
    forall(g, [=](VertexID vsid, G::Vertex& vs) {
        vs.data.weight = 1;
    });
    on_all_cores([] { delegate::reset_cache(); });
}

void do_pagerank(GlobalAddress<G> &g) {

    // intialize parent to -1
    forall(g, [](G::Vertex& v){ v->init(v.nadj); });

    int iter = 0;
    while (iter < 10) {
      double start_time = Grappa::walltime();
      iter++;
      // iterate over all vertices of the graph
      forall(g, [=](VertexID vsid, G::Vertex& vs) {
          auto v = delegate::read(g->vs+vsid);
          if (v.nadj == 0) {
            return;
          }
          bool update = false, init_update = false;
          
          double pr = 0.0;
          forall<SyncMode::Blocking,nullptr>(adj(g,vs), [vsid,&v,&update,&init_update,g,&pr](G::Edge& e){
            auto neighbour = delegate::read(g->vs+e.id);
            if (neighbour.nout == 0) {
                pr += 0;
            }
            else {
                pr += neighbour.data.weight / neighbour.nout;
            } 
          });//forall_here
          pr *= FLAGS_damping; 
          if (abs(v.data.weight - pr) > FLAGS_epsilon) {
            nupdates++;
            v.data.weight = pr;
            delegate::write(g->vs+vsid, v);
          }
      });//forall

      uint32_t total_updates = reduce<uint32_t,collective_sum>(&nupdates);
      LOG(INFO) << "Iteration --> " << iter << " updates " << total_updates <<
        " in " << Grappa::walltime() - start_time << " s.";
      Grappa::mypts() += FLAGS_lease;

      on_all_cores([iter]{ 
          nupdates = 0;
      });
    }//while
}

int main(int argc, char* argv[]) {
  Grappa::init(&argc, &argv);
  Grappa::run([]{
    int64_t NE = (1L << FLAGS_scale) * FLAGS_edgefactor;
    bool directed = true;
    double t;
    
    t = walltime();

    // generate "NE" edge tuples, sampling vertices using the
    // Graph500 Kronecker generator to get a power-law graph
    auto tg = TupleGraph::Kronecker(FLAGS_scale, NE, 111, 222);

    // Twitter has 42M vertices.
    // PagerankData is 8B, tardis_metadata is 20B, while wi_metadata is 32B.
    // Tardis:WI=20:32
    //auto tg = TupleGraph::Load("com-orkut.ungraph.bintsv4", "bintsv4");

    // create graph with incorporated Vertex
    GlobalAddress<G> g;
    if (directed) {
      g = G::Directed( tg );
    }
    else {
      g = G::Undirected( tg );
    }
    graph_create_time = (walltime()-t);
    
    LOG(INFO) << "graph generated (#nodes = " << g->nv << "), " << graph_create_time;

    on_all_cores( [] { Grappa::Metrics::reset(); });
    for (int i = 0; i < NO_TEST; i++) {
      t = walltime();

      do_pagerank(g);

      double this_pagerank_time = walltime() - t;
      pagerank_time += this_pagerank_time;
      LOG(INFO) << "(time=" << this_pagerank_time << ") " <<
        cache_proto_str[FLAGS_cache_proto] << " #E:" << tg.nedge << " #V:" << g->nv;

      if (i < NO_TEST - 1) {
        reset_pagerank(g);
      }
    }
    if (FLAGS_metrics) Metrics::merge_and_print();
    Metrics::merge_and_dump_to_file();

    LOG(INFO) << pagerank_time;

    tg.destroy();
    g->destroy();

  });
  Grappa::finalize();
}
