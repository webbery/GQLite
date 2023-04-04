#pragma once
#include <queue>
#include <functional>
#include "base/parallel/GraphModel.h"

class GNode;
using virtual_graph_t = GMap<uint64_t, uint64_t>;
using node_const_iterator = GMap<uint64_t, uint64_t>::pam_node::const_iterator;
// using virtual_graph_t = GMap<GNode*, std::tuple<GEntityNode*, GAttributeNode*>>;

enum class VisitSelector {
  BreadSearchFirst,
  DeepSearchFirst,
  AStarWalk,
  Dijk,
  RandomWalk
};

/**
 * @brief walk result. If a node is visited, 0 bit will be set 1.
 * 
 */
enum WalkResult {
  WR_UnVisit = 0,
  WR_Visited = 0x1,
  WR_Preload = 0x2,
  WR_Stop = 0x4,
};

using node_info = std::pair<node_t, GMap<uint64_t, uint64_t>::node_collection>;
