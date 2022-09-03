#pragma once
#include <list>
#include <random>
#include "WalkFactory.h"
#include "base/parallel/parlay/sequence.h"
#include <condition_variable>
#include <mutex>

class GRandomWalk {
public:
  GRandomWalk(const std::string& prop, double dumping = 0.9);

  virtual void stand(virtual_graph_t& vg);
  virtual int walk(virtual_graph_t& vg, std::function<void(node_t, const node_info&)>);

private:
  // GVertex* next();
  
private:
  std::string _prop;

  std::default_random_engine _re;
  std::normal_distribution<> _distribution;
  double _dumping;
};
