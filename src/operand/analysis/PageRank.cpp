#include "operand/analysis/PageRank.h"
#include "walk/RandomWalk.h"

#define SIZE_OF_MATRIX_COMPUTATION  10
void GPageRank::analysis(const GVirtualNetwork& g) {
  //if (g.vertex_size() < SIZE_OF_MATRIX_COMPUTATION) {
  //  this->analysisOfMatrix(g);
  //}
  //else {
  //  size_t maxTimes = g.vertex_size() * 20;
  //  for (size_t times = 0; times < maxTimes; ++times) {
  //      
  //  }
  //}
  
}

void GPageRank::analysisOfMatrix(const GVirtualNetwork& g) {}

Eigen::MatrixXd operator - (const GPageRank& left, const GPageRank& right) {
  Eigen::MatrixXd m;
  return m;
}