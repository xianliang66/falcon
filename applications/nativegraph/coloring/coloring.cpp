#include <Grappa.hpp>
#include <GlobalVector.hpp>
#include <graph/Graph.hpp>

#include "coloring.hpp"

#define NO_TEST 3
/* Options */
DEFINE_bool(metrics, false, "Dump metrics");
DEFINE_int32(scale, 20, "Log2 number of vertices.");
DEFINE_int32(edgefactor, 36, "Average number of edges per vertex.");
DEFINE_int32(root, 0, "Vertex whose color is assigned as 1.");

using namespace Grappa;

GRAPPA_DEFINE_METRIC(SummarizingMetric<double>, coloring_time, 0);
GRAPPA_DEFINE_METRIC(SimpleMetric<double>, graph_create_time, 0);

static uint32_t nupdates = 0;
// global completion flag 
bool global_complete = false;
// local completion flag
bool local_complete = false;

void reset_coloring(GlobalAddress<G>& g) {
    forall(g, [=](VertexID vsid, G::Vertex& vs) {
        vs->init();
    });
    on_all_cores([] {
        global_complete = local_complete = false;
        delegate::reset_cache(); 
    });
}

static color_t find_color(std::vector<color_t>& adj_colors, color_t curr) {
    std::sort(adj_colors.begin(), adj_colors.end());

    if (adj_colors[adj_colors.size() - 1] == 0)
      return 0;
    if (adj_colors[0] > 2)
      return 1;

    for (size_t i = 0; i < adj_colors.size(); i++) {
      if (i < adj_colors.size() - 1) {
        if (adj_colors[i + 1] - adj_colors[i] > 2) {
          return adj_colors[i] + 1;
        }
      }
    }
    return adj_colors[adj_colors.size() - 1] + 1;
}

static bool verify_adj(std::vector<color_t> adj) {
    std::sort(adj.begin(), adj.end());
    for (size_t i = 0; i < adj.size(); i++) {
      if (i < adj.size() - 1) {
        if (adj[i] > 0 && adj[i + 1] > 0 && adj[i] == adj[i + 1]) {
          return false; 
        }
      }
    }
    return true;
}

void verify(GlobalAddress<G> &g) {

}

void do_coloring(GlobalAddress<G> &g) {

    // initialize color to 0.
    forall(g, [](G::Vertex& v){ v->init(); });
    VertexID root = FLAGS_root;
    delegate::call(g->vs+root,[=](G::Vertex& v) { 
        v->color = 1;
    });

    int iter = 0;
    while (iter < 10) {
      local_complete = true;
      double start_time = Grappa::walltime();
      iter++;
      // iterate over all vertices of the graph
      forall(g, [=](VertexID vsid, G::Vertex& vs) {
          auto v = delegate::read(g->vs+vsid);
          if (v.nadj == 0) {
            return;
          }
          bool update = false, init_update = false;
          
          std::vector<color_t> adj_colors;
          forall<SyncMode::Blocking,nullptr>(adj(g,vs), [&](G::Edge& e){
            auto neighbour = delegate::read(g->vs+e.id);
            adj_colors.push_back(neighbour.data.color);
          });//forall_here

          color_t mycolor = 0;
          if (std::find(adj_colors.begin(), adj_colors.end(), v.data.color) != adj_colors.end()) {
            mycolor = find_color(adj_colors, v.data.color);
            if (mycolor != 0 && mycolor != v.data.color) {
              update = true;
            }
          }
          if (update) {
            nupdates++;
            v.data.color = mycolor;
            delegate::write(g->vs+vsid, v);
          }
      });//forall

      global_complete = reduce<bool,collective_and>(&local_complete);

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
    bool directed = false;
    double t;
    
    t = walltime();

    // generate "NE" edge tuples, sampling vertices using the
    // Graph500 Kronecker generator to get a power-law graph
    //auto tg = TupleGraph::Kronecker(FLAGS_scale, NE, 111, 222);

    // Twitter has 42M vertices.
    // Coloring is 4B, tardis_metadata is 16B, while wi_metadata is 28B.
    // Tardis:WI=20:32
    auto tg = TupleGraph::Load("com-lj.ungraph.bintsv4", "bintsv4");

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

      do_coloring(g);

      double this_coloring_time = walltime() - t;
      LOG(INFO) << "(time=" << this_coloring_time << ") " <<
        cache_proto_str[FLAGS_cache_proto] << " #E:" << tg.nedge << " #V:" << g->nv;
      coloring_time += this_coloring_time;

      if (i < NO_TEST - 1) {
        reset_coloring(g);
      }
    }

    LOG(INFO) << coloring_time;

    verify(g);

    if (FLAGS_metrics) Metrics::merge_and_print();
    Metrics::merge_and_dump_to_file();

    tg.destroy();
    g->destroy();

  });
  Grappa::finalize();
}
