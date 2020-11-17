#pragma once
#include "../verifier.hpp"

extern int64_t nedge_traversed;

/* Vertex specific data */
struct SSSPData {
  uint8_t dist;
  int32_t parent;
  int32_t level;
  bool seen;

  SSSPData& operator=(const SSSPData& other) {
    this->dist = other.dist;
    this->parent = other.parent;
    this->level = other.level;
    this->seen = other.seen;
    return *this;
  }

  void init(int64_t nadj) {
    dist = std::numeric_limits<uint8_t>::max();
    
    parent = -1;
    level = 0;
    seen = false;
  }
};

struct SSSPEdgeData {
  double weight;
  SSSPEdgeData(): weight(rand() % 128) {}
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
    return delegate::call(g->vs+j, [=](Vertex& v){ 
        for (int k = 0; v.nadj; k++) {
          auto e = g->edge(v,k);
          if (e.id == i)
            return e->weight;
        }
        // we can not reach this, possibly better to throw exception
        return 0.0;
    });
  }

  static inline int64_t verify(TupleGraph tg, GlobalAddress<G> g, int64_t root,
      bool directed) {
    VerificatorBase<G>::verify(tg,g,root,directed);

    // SSSP distances verification
    forall(tg.edges, tg.nedge, [=](TupleGraph::Edge& e){
      auto i = e.v0, j = e.v1;

      /* Eliminate self loops from verification */
      if ( i == j)
        return;

      /* SSSP specific checks */
      auto ti = VerificatorBase<G>::get_parent(g,i), tj = VerificatorBase<G>::get_parent(g,j);
      auto di = get_dist(g,i), dj = get_dist(g,j);
      double wij = get_edge_weight(g,i,j), wji;
      if (!directed) {
        wji = get_edge_weight(g,j,i);
      }
      CHECK(!((di < dj) && ((di + wij) < dj))) << "Error, distance of the nearest neighbor is too great :" 
        << "(" << i << "," << di << ")" << "--" << wij << "-->" <<  "(" << j << "," << dj << ")" ;
      if (!directed)
      CHECK(!((dj < di) && ((dj + wji) < di))) << "Error, distance of the nearest neighbor is too great : "
        << "(" << j << "," << dj << ")" << "--" << wji << "-->" <<  "(" << i << "," << di << ")" ;
      CHECK(!((i == tj) && ((di + wij) != dj))) << "Error, distance of the child vertex is not equil to "
        << "sum of its parent distance and edge weight :" 
        << "(" << i << "," << di << ")" << "--" << wij << "-->" <<  "(" << j << "," << dj << ")" ;
      if (!directed)
      CHECK(!((j == ti) && ((dj + wji) != di))) << "Error, distance of the child vertex is not equil to "
        << "sum of its parent distance and edge weight :"
        << "(" << j << "," << dj << ")" << "--" << wji << "-->" <<  "(" << i << "," << di << ")" ;
    });

    // everything checked out!
    VLOG(1) << "SSSP verified!\n";

    return nedge_traversed;
  }
};

void dump_sssp_graph(GlobalAddress<Graph<SSSPData>> g) {
  for(int i=0; i < g->nv; i++) {
    delegate::call(g->vs+i,[i](typename Graph<SSSPData>::Vertex& v){
      VLOG(1) << "Vertex[" << i << "] -> " << v->dist;
    });
  }
}
