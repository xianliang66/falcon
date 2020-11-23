#pragma once
#include "../verifier.hpp"

typedef uint32_t color_t;
/* Vertex specific data */
struct ColoringData {
  color_t color;

  ColoringData& operator=(const ColoringData& other) {
    this->color = other.color;
    return *this;
  }

  void init(void) {
    color = 0;
  }
};

struct ColoringEdgeData {
};

using G = Graph<ColoringData,ColoringEdgeData>;

