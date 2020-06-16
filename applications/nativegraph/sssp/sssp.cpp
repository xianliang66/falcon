#include <Grappa.hpp>
#include <GlobalVector.hpp>
#include <graph/Graph.hpp>

#include "sssp.hpp"

/* Options */
DEFINE_bool(metrics, false, "Dump metrics");
DEFINE_int32(scale, 10, "Log2 number of vertices.");
DEFINE_int32(edgefactor, 16, "Average number of edges per vertex.");
DEFINE_int64(root, 16, "Index of root vertex.");

using namespace Grappa;

int64_t nedge_traversed;

GRAPPA_DEFINE_METRIC(SummarizingMetric<double>, sssp_mteps, 0);
GRAPPA_DEFINE_METRIC(SummarizingMetric<double>, sssp_time, 0);
GRAPPA_DEFINE_METRIC(SimpleMetric<int64_t>, sssp_nedge, 0);
GRAPPA_DEFINE_METRIC(SimpleMetric<double>, graph_create_time, 0);
GRAPPA_DEFINE_METRIC(SimpleMetric<double>, verify_time, 0);

void dump_sssp_graph(GlobalAddress<G> &g);

static bool terminated(GlobalAddress<unsigned char> complete_addr) {
  bool terminate = true;
  for (int i = 0; i < Grappa::cores(); i++) {
retry:
    unsigned char c = delegate::read(complete_addr + i);
    switch (c) {
      case 0xCA: terminate = false; break;
      // Wait for other threads to finish.
      case 0xBA: Grappa::yield(); LOG(ERROR) << "retry"; goto retry;
      case 0xFF: break;
      // init
      case 0: goto out;
      default: LOG(ERROR) << "Unexcepted " << c; return true;
    }
  }
  if (terminate) {
    return true;
  }
out:
  for (int i = 0; i < Grappa::cores(); i++)
    delegate::write(complete_addr + i, 0xBA);
  return false;
}

void do_sssp(GlobalAddress<G> &g, int64_t root) {
    // set zero value for root distance and
    // setup 'root' as the parent of itself
    auto v = delegate::read(g->vs+root);
    v.data.dist = 0.0;
    v.data.parent = root;
    delegate::write(g->vs+root, v);

    // expose global completion flag to global address space
    GlobalAddress<unsigned char> complete_addr =
      global_alloc<unsigned char>(Grappa::cores());

    int iter = 0;
    while (!terminated(complete_addr)) {
      VLOG(1) << "iteration --> " << iter++;

      // iterate over all vertices of the graph
      on_all_cores([g,complete_addr]{
        // Remote call async?
        bool local_complete = true;
        for (VertexID id = 0; id < g->nv; id++) {
          bool update = false;
          if ((g->vs+id).core() == Grappa::mycore()) {
            auto v = delegate::read(g->vs+id);
            std::vector <VertexID> adjs = g->get_adj(id);
            for (auto iter = adjs.begin(); iter != adjs.end(); iter++) {
              G::Edge e = g->get_edge(id, *iter);
              double neighbor_dist = delegate::read(g->vs+*iter).data.dist;
              double sum = neighbor_dist + e.data.weight;
              if (sum < v.data.dist) {
                v.data.dist = sum;
                v.data.parent = *iter;
                v.data.seen = true;
                local_complete = false;
                update = true;
              }
            }
            if (update) {
              delegate::write(g->vs+id, v);
            }
          }
        }
        if (local_complete) {
          delegate::write(complete_addr + Grappa::mycore(), 0xFF);
        }
        else {
          delegate::write(complete_addr + Grappa::mycore(), 0xCA);
        }
    });
  }
}

int main(int argc, char* argv[]) {
  Grappa::init(&argc, &argv);
  Grappa::run([]{
    int64_t NE = (1L << FLAGS_scale) * FLAGS_edgefactor;
    bool verified = false;
    double t;

    t = walltime();

    // generate "NE" edge tuples, sampling vertices using the
    // Graph500 Kronecker generator to get a power-law graph
    auto tg = TupleGraph::Kronecker(FLAGS_scale, NE, 111, 222);

    // create graph with incorporated Vertex
    auto g = G::Undirected( tg );
    graph_create_time = (walltime()-t);

    LOG(INFO) << "graph generated (#nodes = " << g->nv << "), " << graph_create_time;

    t = walltime();

    auto root = FLAGS_root;
    do_sssp(g, root);

    double this_sssp_time = walltime() - t;
    LOG(INFO) << "(root=" << root << ", time=" << this_sssp_time << ")";
    sssp_time += this_sssp_time;

    if (!verified) {
      // only verify the first one to save time
      t = walltime();
      sssp_nedge = Verificator<G>::verify(tg, g, root);
      verify_time = (walltime()-t);
      LOG(INFO) << verify_time;
      verified = true;
    }
    sssp_mteps += sssp_nedge / this_sssp_time / 1.0e6;

    LOG(INFO) << "\n" << sssp_nedge << "\n" << sssp_time << "\n" << sssp_mteps;

    if (FLAGS_metrics) Metrics::merge_and_print();
    Metrics::merge_and_dump_to_file();

    // dump graph after computation
    //dump_sssp_graph(g);

    tg.destroy();
    g->destroy();

  });
  Grappa::finalize();
}
