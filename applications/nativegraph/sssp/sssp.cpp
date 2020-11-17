#include <Grappa.hpp>
#include <GlobalVector.hpp>
#include <graph/Graph.hpp>

#include "sssp.hpp"

#define NO_TEST 5
/* Options */
DEFINE_bool(metrics, false, "Dump metrics");
DEFINE_int32(scale, 18, "Log2 number of vertices.");
DEFINE_int32(edgefactor, 36, "Average number of edges per vertex.");
DEFINE_int64(root, 1, "Average number of edges per vertex.");

using namespace Grappa;

int64_t nedge_traversed;

GRAPPA_DEFINE_METRIC(SummarizingMetric<double>, sssp_time, 0);
GRAPPA_DEFINE_METRIC(SimpleMetric<int64_t>, sssp_nedge, 0);
GRAPPA_DEFINE_METRIC(SimpleMetric<double>, graph_create_time, 0);
GRAPPA_DEFINE_METRIC(SimpleMetric<double>, verify_time, 0);

void dump_sssp_graph(GlobalAddress<G> &g);

// global completion flag 
bool global_complete = false;
// local completion flag
bool local_complete = false;

static uint32_t nupdates = 0;

void reset_sssp(GlobalAddress<G>& g) {
    forall(g, [=](VertexID vsid, G::Vertex& vs) {
      vs.data.dist = std::numeric_limits<uint8_t>::max();
      vs.data.parent = -1;
      vs.data.level = 0;
      vs.data.seen = false;
    });
    on_all_cores([] { global_complete = local_complete = false; });
    on_all_cores([] { delegate::reset_cache(); });
}

void do_sssp(GlobalAddress<G> &g, int64_t root) {

    // intialize parent to -1
    forall(g, [](G::Vertex& v){ v->init(v.nadj); });

    VLOG(1) << "root => " << root;

    // set zero value for root distance and
    // setup 'root' as the parent of itself
    delegate::call(g->vs+root,[=](G::Vertex& v) { 
      v->dist = 0.0;
      v->parent = root;
    });

    // expose global completion flag to global address space
    GlobalAddress<bool> complete_addr = make_global(&global_complete);

    int iter = 0;
    while (!global_complete) {
      double start_time = Grappa::walltime();
      global_complete = true;
      iter++;
      // iterate over all vertices of the graph
      forall(g, [=](VertexID vsid, G::Vertex& vs) {
          auto v = delegate::read(g->vs+vsid);
          if (v.nadj == 0) {
            return;
          }
          bool update = false, init_update = false;

          if (!v.data.seen) {
            init_update = true;
            update = true;
            v.data.seen = true;
          }
          
          forall<SyncMode::Blocking,nullptr>(adj(g,vs), [vsid, &v,&update,&init_update,g](G::Edge& e){
            // calculate potentinal new distance and...
            auto neighbour = delegate::read(g->vs+e.id);
            double new_dist = neighbour.data.dist + e->weight;
            if (new_dist < v.data.dist) {
              local_complete = false;
              update = true;
              init_update = false;
              v.data.dist = new_dist;
              v.data.parent = e.id;
            }
          });//forall_here
          if (update) {
            if (!init_update)
              nupdates++;
            delegate::write(g->vs+vsid, v);
          }
      });//forall

      // find if SSSP calculation is completed (must be completed
      // in each core)
      global_complete = reduce<bool,collective_and>(&local_complete);

      uint32_t total_updates = reduce<uint32_t,collective_sum>(&nupdates);
      LOG(INFO) << "Iteration --> " << iter << " updates " << total_updates <<
        " in " << Grappa::walltime() - start_time << " s.";

      on_all_cores([iter]{ 
          local_complete = true;
          nupdates = 0;
      });
    }//while
}

int main(int argc, char* argv[]) {
  Grappa::init(&argc, &argv);
  Grappa::run([]{
    int64_t NE = (1L << FLAGS_scale) * FLAGS_edgefactor;
    bool verified = false;
    bool directed = true;
    double t;
    
    t = walltime();

    // generate "NE" edge tuples, sampling vertices using the
    // Graph500 Kronecker generator to get a power-law graph
    auto tg = TupleGraph::Kronecker(FLAGS_scale, NE, 111, 222);

    // Twitter has 42M vertices.
    // SSSPData is 12B, tardis_metadata is 28B, while wi_metadata is 40B.
    // Tardis:WI=40:52
    //auto tg = TupleGraph::Load("twitter_bintsv4.net", "bintsv4");

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
    auto root = FLAGS_root;
    for (int i = 0; i < NO_TEST; i++) {
      t = walltime();

      do_sssp(g, root);

      double this_sssp_time = walltime() - t;
      LOG(INFO) << "(root=" << root << ", time=" << this_sssp_time << ") " <<
        cache_proto_str[FLAGS_cache_proto] << " #E:" << tg.nedge << " #V:" << g->nv;
      sssp_time += this_sssp_time;

      if (i < NO_TEST - 1) {
        reset_sssp(g);
      }
    }
    if (FLAGS_metrics) Metrics::merge_and_print();
    Metrics::merge_and_dump_to_file();

    LOG(INFO) << sssp_time;

    if (!verified) {
      // only verify the first one to save time
      t = walltime();
      sssp_nedge = Verificator<G>::verify(tg, g, root, directed);
      verify_time = (walltime()-t);
      LOG(INFO) << verify_time;
      verified = true;
    }

    tg.destroy();
    g->destroy();

  });
  Grappa::finalize();
}
