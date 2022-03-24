#pragma once
#include "base/Serializer.h"
#include "base/Statement.h"
#include "json.hpp"

enum class GraphEdgeDirection {
  Bidrection,
  To,
};

class edge_id {
public:
  edge_id(const std::string& from, const std::string& to, GraphEdgeDirection direction = GraphEdgeDirection::Bidrection);
  edge_id(const std::string& eid);

  bool operator == (const edge_id& other);

  operator std::string();
private:
  std::string _from;
  std::string _to;
  GraphEdgeDirection _direction;
};

class GLiteralEdge {};

class GAttributeEdge {};

class GEdge {
public:
  GEdge(const std::string& from, const std::string& to, GraphEdgeDirection direction);
  virtual ~GEdge() {}

  std::string id();
protected:
  edge_id _id;
  nlohmann::json _json;
};

class GEdgeStatment: public GSerializer, public GStatement, public GEdge {
public:
  GEdgeStatment();
  virtual int Parse(struct gast* ast);
  virtual int Dump();
  virtual std::vector<uint8_t> serialize();
  virtual void deserialize(uint8_t* data, size_t len);
};