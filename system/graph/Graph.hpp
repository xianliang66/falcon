////////////////////////////////////////////////////////////////////////
// Copyright (c) 2010-2015, University of Washington and Battelle
// Memorial Institute.  All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//     * Redistributions of source code must retain the above
//       copyright notice, this list of conditions and the following
//       disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials
//       provided with the distribution.
//     * Neither the name of the University of Washington, Battelle
//       Memorial Institute, or the names of their contributors may be
//       used to endorse or promote products derived from this
//       software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
// FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
// UNIVERSITY OF WASHINGTON OR BATTELLE MEMORIAL INSTITUTE BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
// OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
// BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
// USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
// DAMAGE.
////////////////////////////////////////////////////////////////////////

#pragma once

#include <Communicator.hpp>
#include <Addressing.hpp>
#include <Collective.hpp>
#include <ParallelLoop.hpp>
#include <GlobalAllocator.hpp>
#include <Delegate.hpp>
#include <AsyncDelegate.hpp>
#include <Array.hpp>
#include "TupleGraph.hpp"

#include <algorithm>
#include <iomanip>
#include <vector>

// #define USE_MPI3_COLLECTIVES
#undef USE_MPI3_COLLECTIVES
#ifdef USE_MPI3_COLLECTIVES
#include <mpi.h>
#endif

namespace Grappa {
  /// @addtogroup Graph
  /// @{

  /// Currently just an overload for int64, may someday be used for distinguishing parameters in forall().
  using VertexID = int64_t;

  /// Empty struct, for specifying lack of either Vertex or Edge data in @ref Graph.
  struct Empty {};

  namespace impl {
    /// Vertex with customizable inline 'data' field. Will attempt
    /// to pack the provided type into the block-aligned Vertex
    /// class, but if it is too large, will heap-allocate (from
    /// locale-shared heap). Defines a '->' operator to access data
    /// fields.
    ///
    /// Example subclasses:
    /// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    /// struct Parent { int64_t parent; };
    ///
    /// Vertex<Parent> p;
    /// p->parent = -1;
    ///
    /// struct VertexP : public Vertex<int64_t> {
    ///   VertexP(): Vertex() { parent(-1); }
    ///   int64_t parent() { return data; }
    ///   void parent(int64_t parent) { data = parent; }
    /// };
    /// Edge info is now stored in Graph's adjacent matrix.
    /// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    template< typename T, bool HeapData = (sizeof(T) > BLOCK_SIZE) >
    struct Vertex {
      T data;

      Vertex() {}
      Vertex(const Vertex& v): data(v.data) {}
      ~Vertex() {}

      T* operator->() { return &data; }
      const T* operator->() const { return &data; }

      static constexpr size_t global_heap_size() { return sizeof(Vertex); }
      static constexpr size_t locale_heap_size() { return 0; }
      static constexpr size_t size() { return locale_heap_size() + global_heap_size(); }

    } GRAPPA_BLOCK_ALIGNED;

    template< typename T>
    struct Vertex<T, /*HeapData = */ true> {
      T& data;

      Vertex(): data(*locale_alloc<T>()) {}
      Vertex(const Vertex& v): data(*locale_alloc<T>()) {}

      ~Vertex() { locale_free(&data); }

      T* operator->() { return &data; }
      const T* operator->() const { return &data; }

      static constexpr size_t global_heap_size() { return sizeof(Vertex); }
      static constexpr size_t locale_heap_size() { return sizeof(T); }
      static constexpr size_t size() { return locale_heap_size() + global_heap_size(); }

    } GRAPPA_BLOCK_ALIGNED;
  }

  /// Distributed graph data structure, with customizable vertex and edge data.
  ///
  /// This is Grappa's primary graph data structure. Graph is a
  /// *symmetric data structure*, so a Graph proxy object will be allocated
  /// on all cores, providing local access to common data and methods to
  /// access the entirety of the graph.
  ///
  /// The Graph class defines two types based on the specified template
  /// parameters: Vertex and Edge. Vertex holds information about an
  /// individual vertex, such as the degree (`nadj`), and the associated
  /// data whose type is specified by the `V` template parameter.
  /// Edge holds the two global addresses to its source and destination
  /// vertices, as well as data associated with this edge, of the type
  /// specified by the `E` parameter.
  ///
  /// A typical use of Graph will define a custom graph type, construct it,
  /// and then use the returned symmetric pointer to refer to the graph,
  /// possibly looking something like this:
  /// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  /// // define the graph type:
  /// struct VertexData { double rank; };
  /// struct EdgeData { double weight; };
  /// using G = Graph<VertexData,EdgeData>;
  ///
  /// // load tuples from a file:
  /// TupleGraph tuples = TupleGraph::Load("twitter.bintsv4", "bintsv4");
  ///
  /// // after constructing a graph, we get a symmetric pointer back
  /// GlobalAddress<G> g = G::create(tuples);
  ///
  /// // we can easily get info such as the number of vertices out by
  /// // using the local proxy directly through the global address:
  /// LOG(INFO) << "graph has " << g->nv << " vertices";
  ///
  /// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  ///
  /// Graph Structure
  /// ----------------
  ///
  /// This Graph structure is constructed from a TupleGraph (a simple list
  /// of edge tuples -- source & dest) using Graph::create().
  ///
  /// Vertices are randomly distributed among cores (using a simple global
  /// heap allocation). Edges are placed on the core of their *source* vertex.
  /// Therefore, iterating over outgoing edges is very efficient, but a
  /// vertex's incoming edges must be found the hard way (in practice,
  /// we just avoid doing it entirely if using this graph structure).
  ///
  template< typename V = Empty, typename E = Empty >
  struct Graph {

    using Vertex = impl::Vertex<V>;
    using EdgeState = E;

    struct Edge {
      EdgeState data;
      bool valid;

      Edge(): valid(false) {}
      /// Access elements of EdgeState with operator '->'
      EdgeState* operator->() { return &data; }
      const EdgeState* operator->() const { return &data; }
    };

    static_assert(block_size % sizeof(Vertex) == 0, "V size not evenly divisible into blocks!");

    // using Vertex = V;

    // // Helpers (for if we go with custom cyclic distribution)
    // inline Core    vertex_owner (int64_t v) { return v % cores(); }
    // inline int64_t vertex_offset(int64_t v) { return v / cores(); }

    // Fields
    GlobalAddress<Vertex> vs;
    int64_t nv;

    // Adjacent matrix (nv*nv)
    Edge ** edge_storage;

    GlobalAddress<Graph> self;

    Graph(GlobalAddress<Graph> self, GlobalAddress<Vertex> vs, int64_t nv)
      : self(self)
      , vs(vs)
      , nv(nv)
      , edge_storage(nullptr)
    { }

    ~Graph() {
      for (Vertex& v : iterate_local(vs, nv)) { v.~Vertex(); }
      if (edge_storage) {
        for (int64_t i=0; i<nv; i++) {
          for (int64_t j=0; j<nv; j++) {
            edge_storage[i][j].data.~E();
          }
        }
        locale_free(edge_storage);
      }
    }

    std::vector<VertexID> get_adj(VertexID id) {
      std::vector <VertexID> r;
      for (VertexID iter = 0; iter<nv; iter++) {
        if (edge_storage[id][iter].valid) {
          r.push_back(iter);
        }
      }
      return r;
    }

    Edge get_edge(VertexID s, VertexID d) {
      return edge_storage[s][d];
    }

    void destroy() {
      auto self = this->self;
      global_free(this->vs);
      call_on_all_cores([self]{ self->~Graph(); });
      global_free(self);
    }

    template< int LEVEL = 0 >
    static void dump(GlobalAddress<Graph> g) {
      for (int64_t i=0; i<g->nv; i++) {
        for (int64_t j=0; j<g->nv; j++) {
          if (g->edge_storage[i][j].valid) {
            std::stringstream ss;
            ss << "<" << i << ">" << " " << j;
            VLOG(LEVEL) << ss.str();
          }
        }
      }
    }

    template< int LEVEL = 0, typename F = nullptr_t >
    void dump(F print_vertex) {
      for (int64_t i=0; i<nv; i++) {
        for (int64_t j=0; j<nv; j++) {
            if (edge_storage[i][j].valid) {
            std::stringstream ss;
            ss << "<" << std::setw(2) << i << ">" << " " << j;
            print_vertex(ss, vs+i);
          }
        }
      }
    }

    // Constructor
    static GlobalAddress<Graph> create(const TupleGraph& tg, bool directed = false, bool solo_invalid = true);

    static GlobalAddress<Graph> Undirected(const TupleGraph& tg) { return create(tg, false); }
    static GlobalAddress<Graph> Directed(const TupleGraph& tg) { return create(tg, true); }

  } GRAPPA_BLOCK_ALIGNED;

  /// @brief Construct a distributed adjacency-matrix Graph.
  ///
  /// @return The symmetric address to the 'proxy' allocated on each core,
  ///         which has the size information and a portion of the graph.
  ///
  /// @param tg            input edges
  /// @param directed      create additional edges to make it undirected
  /// @param solo_invalid  mark vertices with no in- or out-edges as
  ///                      invalid (not to be visited when iterating
  ///                      over vertices)
  template< typename V, typename E >
  GlobalAddress<Graph<V,E>> Graph<V,E>::create(const TupleGraph& tg,
      bool directed, bool solo_invalid) {
    VLOG(1) << "Graph: " << (directed ? "directed" : "undirected");
    double t;
    auto g = symmetric_global_alloc<Graph>();

    // find nv
    t = walltime();
    forall(tg.edges, tg.nedge, [g](TupleGraph::Edge& e){
      if (e.v0 > g->nv) { g->nv = e.v0; }
      if (e.v1 > g->nv) { g->nv = e.v1; }
    });
    on_all_cores([g]{
      g->nv = Grappa::allreduce<int64_t,collective_max>(g->nv) + 1;
      g->edge_storage = locale_alloc<Edge*>(g->nv);
      for (size_t i=0; i<g->nv; i++) {
        g->edge_storage[i] = locale_alloc<Edge>(g->nv);
      }
      for (size_t i=0; i<g->nv; i++) {
        // Edge data is given by applications (default constructor of EdgeData
        // rather than input data. They're immutable.
        for (size_t j=0; j<g->nv; j++) {
          g->edge_storage[i][j] = Edge();
        }
      }
    });

    auto vs = global_alloc<Vertex>(g->nv);
    auto self = g;
    on_all_cores([g,vs]{
      for (Vertex& v : iterate_local(g->vs, g->nv)) {
        new (&v) Vertex();
      }
    });
    t = walltime();

    // Set topology of graph
    on_all_cores([g,tg,directed] {
      for (size_t i=0; i<tg.nedge; i++) {
        auto e = delegate::read(tg.edges + i);
        VertexID s = e.v0;
        VertexID d = e.v1;
        CHECK_LT(s, g->nv); CHECK_LT(d, g->nv);
        if (s != d) {
          g->edge_storage[s][d].valid = true;
        }
        if (s != d && !directed) {
          g->edge_storage[d][s] = g->edge_storage[s][d];
        }
      }
    });

    VLOG(1) << "-- vertices: " << g->nv;

    auto gsz = Vertex::global_heap_size()*g->nv
                          + sizeof(Graph) * cores();
    auto lsz = Vertex::locale_heap_size()*g->nv
                          + (sizeof(Edge))*g->nv*g->nv;
    auto GB = [](size_t v){ return static_cast<double>(v) / (1L<<30); };
    LOG(INFO) << "\nGraph memory breakdown:"
              << "\n  locale_heap_size: " << GB(lsz) << " GB"
              << "\n  global_heap_size: " << GB(gsz) << " GB"
              << "\n  graph_total_size: " << GB(lsz+gsz) << " GB";
    return g;
  }

  /// @}
} // namespace Grappa
