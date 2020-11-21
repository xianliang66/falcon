#pragma once
#include "../verifier.hpp"

/* Vertex specific data */
struct PagerankData {
  double weight;

  PagerankData& operator=(const PagerankData& other) {
    this->weight = other.weight;
    return *this;
  }

  void init(int64_t nadj) {
    weight = 1.0;
  }
};

struct PagerankEdgeData {
};

using G = Graph<PagerankData,PagerankEdgeData>;

