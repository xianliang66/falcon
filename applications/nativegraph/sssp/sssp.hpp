#pragma once
#include "../verifier.hpp"

extern int64_t nedge_traversed;

/* Vertex specific data */
struct SSSPData {
  double dist;
  VertexID parent;

  SSSPData() {
    dist = std::numeric_limits<double>::max() / 2;
  }
};

static unsigned long hash(char *s, int l)
{
  unsigned long hash = 5381;
  char c;

  for (unsigned i = 0; i < l; i++) {
    c = s[i];
    hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
  }

  return hash;
}

struct SSSPEdgeData {
  double weight;
  // (source, dest). The value should be agreed by all cores.
  SSSPEdgeData(int i, int j) {
    int s = (i + ~12)* (i + 0x7)* (j +~12)* (j +0x7)+ ~264;
    weight = hash((char *)&s, sizeof(weight)) % 1024;
  }
  SSSPEdgeData() {}
};

using G = Graph<SSSPData,SSSPEdgeData>;

template <typename G>
class Verificator : public VerificatorBase<G> {
  using Vertex = typename G::Vertex;
public:

  static double get_dist(GlobalAddress<G> g, int64_t j) {
    return delegate::read(g->vs+j).data.dist;
  }

  static double get_edge_weight(GlobalAddress<G> g, int64_t i, int64_t j) {
    return g->get_edge(i, j).data.weight;
  }

  static inline int64_t verify(TupleGraph tg, GlobalAddress<G> g, int64_t root) {
    //VerificatorBase<G>::verify(tg,g,root);

    // SSSP distances verification
    forall(tg.edges, tg.nedge, [=](TupleGraph::Edge& e){
      auto i = e.v0, j = e.v1;

      /* Eliminate self loops from verification */
      if ( i == j)
        return;

      /* SSSP specific checks */
      auto ti = VerificatorBase<G>::get_parent(g,i), tj = VerificatorBase<G>::get_parent(g,j);
      auto di = get_dist(g,i), dj = get_dist(g,j);
      auto wij = get_edge_weight(g,i,j), wji = get_edge_weight(g,j,i);
      CHECK(!((di < dj) && ((di + wij) < dj))) << "Error, distance of the nearest neighbor is too great :"
        << "(" << i << "," << di << ")" << "--" << wij << "-->" <<
        "(" << j << "," << dj << ") by Core " << Grappa::mycore();
      CHECK(!((dj < di) && ((dj + wji) < di))) << "Error, distance of the nearest neighbor is too great : "
        << "(" << j << "," << dj << ")" << "--" << wji << "-->" <<
        "(" << i << "," << di << ") by Core " << Grappa::mycore();
      CHECK(!((i == tj) && ((di + wij) != dj))) << "Error, distance of the child vertex is not equil to "
        << "sum of its parent distance and edge weight :"
        << "(" << i << "," << di << ")" << "--" << wij << "-->" <<
        "(" << j << "," << dj << ") by Core " << Grappa::mycore();
      CHECK(!((j == ti) && ((dj + wji) != di))) << "Error, distance of the child vertex is not equil to "
        << "sum of its parent distance and edge weight :"
        << "(" << j << "," << dj << ")" << "--" << wji << "-->" <<
        "(" << i << "," << di << ") by Core " << Grappa::mycore();
    });

    // everything checked out!
    LOG(ERROR) << "SSSP verified!\n";

    return nedge_traversed;
  }
};

void dump_sssp_graph(GlobalAddress<Graph<SSSPData>> g) {
  for(int i=0; i < g->nv; i++) {
    VLOG(1) << "Vertex[" << i << "] -> " << delegate::read(g->vs+i).data.dist;
  }
}
