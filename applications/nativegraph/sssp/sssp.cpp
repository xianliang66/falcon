#include <Grappa.hpp>
#include <GlobalVector.hpp>
#include <graph/Graph.hpp>

#include "sssp.hpp"

/* Options */
DEFINE_bool(metrics, false, "Dump metrics");
DEFINE_int32(scale, 23, "Log2 number of vertices.");
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

static bool emergency_stop = false;

static bool terminated(GlobalAddress<thread_state> complete_addr) {
  int nterm = 0;
  for (Core i = 0; i < Grappa::cores(); i++) {
    thread_state ts = delegate::read(complete_addr + i);
    if (ts == TERMINATE) {
      nterm++;
    }
  }
  if (emergency_stop || nterm >= Grappa::cores()) {
    for (Core i = 0; i < Grappa::cores(); i++) {
      delegate::call(i, [] { emergency_stop = true; });
    }
    return true;
  }
  else {
    return false;
  }
}

// Active vertex
static std::queue<VertexID> active_queue;
static std::unordered_map<VertexID, std::vector<VertexID>> active_edges;

static void dump_active(const std::unordered_map<VertexID, std::vector<VertexID>>& active) {
  for (auto i = active.begin(); i != active.end(); i++) {
    for (auto j = i->second.begin(); j != i->second.end(); j++) {
      LOG(ERROR) << "Core " << Grappa::mycore() << " " << *j << "==>" << i->first;
    }
  }
}

void do_sssp(GlobalAddress<G> &g, int64_t root) {
    // set zero value for root distance and
    // setup 'root' as the parent of itself
    auto v = delegate::read(g->vs+root);
    v.data.dist = 0.0;
    v.data.parent = root;
    delegate::write(g->vs+root, v);

    on_all_cores([g,root]{
      if ((g->vs+root).is_owner()) {
        const std::vector <VertexID>& adjs = g->get_out_vertices(root);

        for (auto iter = adjs.begin(); iter != adjs.end(); iter++) {
          VertexID vout = *iter;
          delegate::call((g->vs+vout).core(), [vout,root] {
              if (active_edges[vout].size() == 0) {
                active_queue.push(vout);
              }
              active_edges[vout].push_back(root);
          });
        }
      }
    });

    // expose global completion flag to global address space
    GlobalAddress<thread_state> complete_addr =
      global_alloc<thread_state>(Grappa::cores());

    // iterate over all vertices of the graph
    on_all_cores([g,complete_addr]{
      int iter = 0;
      int nupdates = 0;
      CompletionEvent ce;

      do {
        double start_time = Grappa::walltime();
        while (active_queue.size() > 0) {
          VertexID id = active_queue.front();
          active_queue.pop();
          CHECK((g->vs+id).is_owner());
          bool update = false;

          auto adjs = active_edges[id];
          active_edges[id].clear();
          // Local read
          auto v = delegate::read(g->vs+id);

          for (auto adj_iter = adjs.begin(); !emergency_stop &&
              adj_iter != adjs.end(); adj_iter++) {

            ce.enroll();
            spawn([g, adj_iter, id, &v, &update, &ce] {
              double neighbor_dist = delegate::read(g->vs+*adj_iter).data.dist;

              G::Edge e = g->get_edge(*adj_iter, id);
              double sum = neighbor_dist + e.data.weight;
              if (sum < v.data.dist) {
                v.data.dist = sum;
                v.data.parent = *adj_iter;
                update = true;
              }
              ce.complete();
            });
          }
          ce.wait();

          if (update) {
            nupdates++;
            delegate::write(g->vs+id, v);
            // Activate all out-edges' neighbours
            const std::vector<VertexID>& out = g->get_out_vertices(id);

            for (auto iter = out.begin(); !emergency_stop &&
              iter != out.end(); iter++) {

              VertexID vout = *iter;

              spawn([g, vout, id, &ce] {
                delegate::call((g->vs+vout).core(), [vout, id] {
                    if (active_edges[vout].size() == 0) {
                      active_queue.push(vout);
                    }
                    active_edges[vout].push_back(id);
                });
              });
            }
          }
        }
        delegate::write(complete_addr + Grappa::mycore(), TERMINATE);
      } while (!emergency_stop && !terminated(complete_addr));

      LOG(ERROR) << "Core " << Grappa::mycore() << " updates " << nupdates << " vertices.";
    });
}

int main(int argc, char* argv[]) {
  Grappa::init(&argc, &argv);
  Grappa::run([]{
    int64_t NE = (1L << FLAGS_scale) * FLAGS_edgefactor;
    bool verified = true;
    bool directed = true;
    double t;

    t = walltime();

    // generate "NE" edge tuples, sampling vertices using the
    // Graph500 Kronecker generator to get a power-law graph
    //auto tg = TupleGraph::Kronecker(FLAGS_scale, NE, 111, 222);
    auto tg = TupleGraph::Load("twitter_bintsv4.net", "bintsv4");

    // create graph with incorporated Vertex
    GlobalAddress<G> g;
    if (directed) {
      g = G::Directed( tg );
    }
    else {
      g = G::Undirected( tg );
    }

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
      MAX_CACHE_NUMBER * 1.0 / g->nv * 100 << "%";
    sssp_time += this_sssp_time;

    if (FLAGS_metrics) Metrics::merge_and_print();
    Metrics::merge_and_dump_to_file();

    if (!verified) {
      // only verify the first one to save time
      t = walltime();
      sssp_nedge = Verificator<G>::verify(tg, g, root, directed);
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
