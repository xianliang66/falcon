#include <Grappa.hpp>
#include <GlobalVector.hpp>
#include <graph/Graph.hpp>

#include "sssp.hpp"

/* Options */
DEFINE_bool(metrics, false, "Dump metrics");
DEFINE_int32(scale, 18, "Log2 number of vertices.");
DEFINE_int32(edgefactor, 17, "Average number of edges per vertex.");
DEFINE_int64(root, 12, "Index of root vertex.");

using namespace Grappa;

int64_t nedge_traversed;

GRAPPA_DEFINE_METRIC(SummarizingMetric<double>, sssp_mteps, 0);
GRAPPA_DEFINE_METRIC(SummarizingMetric<double>, sssp_time, 0);
GRAPPA_DEFINE_METRIC(SimpleMetric<int64_t>, sssp_nedge, 0);
GRAPPA_DEFINE_METRIC(SimpleMetric<double>, graph_create_time, 0);
GRAPPA_DEFINE_METRIC(SimpleMetric<double>, verify_time, 0);

void dump_sssp_graph(GlobalAddress<G> &g);

enum thread_state { INIT = 0x0, RUNNING = 0xBA, UPDATE = 0xCA, TERMINATE = 0xFF };

static bool terminated(GlobalAddress<thread_state> complete_addr) {
  for (Core i = 0; i < Grappa::cores(); i++) {
    thread_state ts = delegate::read(complete_addr + i);
    if (ts == UPDATE || ts == INIT) {
      return false;
    }
  }
  return true;
}

static int nupdates = 0;

void do_sssp(GlobalAddress<G> &g, int64_t root) {
    // set zero value for root distance and
    // setup 'root' as the parent of itself
    auto v = delegate::read(g->vs+root);
    v.data.dist = 0.0;
    v.data.parent = root;
    delegate::write(g->vs+root, v);

    // expose global completion flag to global address space
    GlobalAddress<thread_state> complete_addr =
      global_alloc<thread_state>(Grappa::cores());

    static bool local_complete = true;
    // iterate over all vertices of the graph
    on_all_cores([g,complete_addr]{
      int iter = 0;
      CompletionEvent ce;
      while (!terminated(complete_addr)) {
        local_complete = true;
        for (VertexID id = 0; id < g->nv; id++) {
          if ((g->vs+id).core() == Grappa::mycore()) {
            bool update = false;
            // Local read
            auto v = delegate::read(g->vs+id);
            const std::vector <G::Edge>& adjs = g->get_adj(id);

            for (auto iter = adjs.begin(); iter != adjs.end(); iter++) {

              // Concurrent reads of neighbors.
              auto update_func = [g, id, iter, &v, &update, &ce] {
                double neighbor_dist = delegate::read(g->vs+iter->fromId).data.dist;

                G::Edge e = g->get_edge(id, iter->fromId);
                double sum = neighbor_dist + e.data.weight;
                if (sum < v.data.dist) {
                  v.data.dist = sum;
                  v.data.parent = iter->fromId;
                  local_complete = false;
                  update = true;
                }
                ce.complete();
              };
              ce.enroll();
              spawn(update_func);
            }
            ce.wait();
            if (update) {
              nupdates++;
              delegate::write(g->vs+id, v);
            }
          }
        }
        LOG(ERROR) << "Core " << Grappa::mycore() << " iteration " << ++iter
          << " updates " << nupdates << " vertices.";
        nupdates = 0;
        if (local_complete) {
          delegate::write(complete_addr + Grappa::mycore(), TERMINATE);
        }
        else {
          delegate::write(complete_addr + Grappa::mycore(), UPDATE);
        }
      }
    });
}

int main(int argc, char* argv[]) {
  Grappa::init(&argc, &argv);
  Grappa::run([]{
    int64_t NE = (1L << FLAGS_scale) * FLAGS_edgefactor;
    bool verified = true;
    double t;

    t = walltime();

    // generate "NE" edge tuples, sampling vertices using the
    // Graph500 Kronecker generator to get a power-law graph
    //auto tg = TupleGraph::Kronecker(FLAGS_scale, NE, 111, 222);
    auto tg = TupleGraph::Load("twitter_rv.net", "tsv");

    // create graph with incorporated Vertex
    auto g = G::Directed( tg );

    graph_create_time = (walltime()-t);

    LOG(ERROR) << "graph generated (#nodes = " << g->nv << " #edges = " <<
    tg.nedge << "), " << graph_create_time;

    t = walltime();

    Metrics::reset_all_cores();
    Metrics::start_tracing();

    auto root = FLAGS_root;
    do_sssp(g, root);

    double this_sssp_time = walltime() - t;
    LOG(ERROR) << "(root=" << root << ", time=" << this_sssp_time << ") proto:"
      << GRAPPA_CC_PROTOCOL_NAME << " #Cache:" <<
      MAX_CACHE_NUMBER * 1.0 / (1L << FLAGS_scale) * 100 << "%";
    sssp_time += this_sssp_time;

    if (FLAGS_metrics) Metrics::merge_and_print();
    Metrics::merge_and_dump_to_file();

    if (!verified) {
      // only verify the first one to save time
      t = walltime();
      sssp_nedge = Verificator<G>::verify(tg, g, root);
      verify_time = (walltime()-t);
      LOG(ERROR) << verify_time;
      verified = true;
    }
    sssp_mteps += sssp_nedge / this_sssp_time / 1.0e6;

    g->destroy();
    tg.destroy();

  });
  Grappa::finalize();
}
