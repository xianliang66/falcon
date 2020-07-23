#include <Grappa.hpp>
#include <GlobalVector.hpp>
#include <graph/Graph.hpp>

#include "sssp.hpp"

/* Options */
DEFINE_bool(metrics, false, "Dump metrics");
DEFINE_int32(scale, 15, "Log2 number of vertices.");
DEFINE_int32(edgefactor, 128, "Average number of edges per vertex.");
DEFINE_int64(root, 1, "Index of root vertex.");

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

    int iter = 0;
    static bool local_complete = true;
    while (!terminated(complete_addr)) {
      LOG(ERROR) << "iteration --> " << iter++;

      // iterate over all vertices of the graph
      on_all_cores([g,complete_addr]{
        // Remote call async
        local_complete = true;
        for (VertexID id = 0; id < g->nv; id++) {
          if ((g->vs+id).core() == Grappa::mycore()) {
            auto func = [g,id] {
              bool update = false;
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
                nupdates++;
                delegate::write(g->vs+id, v);
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

    LOG(ERROR) << "graph generated (#nodes = " << g->nv << "), " << graph_create_time;

    t = walltime();

    auto root = FLAGS_root;
    do_sssp(g, root);

    double this_sssp_time = walltime() - t;
    LOG(ERROR) << "(root=" << root << ", time=" << this_sssp_time << ") proto:"
      << GRAPPA_CC_PROTOCOL_NAME;
    sssp_time += this_sssp_time;

    if (!verified) {
      // only verify the first one to save time
      t = walltime();
      sssp_nedge = Verificator<G>::verify(tg, g, root);
      verify_time = (walltime()-t);
      LOG(ERROR) << verify_time;
      verified = true;
    }
    sssp_mteps += sssp_nedge / this_sssp_time / 1.0e6;

    LOG(ERROR) << "\n" << sssp_nedge << "\n" << sssp_time << "\n" << sssp_mteps;

    if (FLAGS_metrics) Metrics::merge_and_print();
    Metrics::merge_and_dump_to_file();

    // dump graph after computation
    //dump_sssp_graph(g);

    tg.destroy();
    g->destroy();

  });
  Grappa::finalize();
}
