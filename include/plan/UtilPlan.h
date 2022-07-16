#pragma once
#include "Plan.h"
#include <variant>
#include <string>
#include "base/lang/ASTNode.h"

struct GASTNode;
class GVirtualNetwork;
class GStorageEngine;
class GUtilPlan: public GPlan {
public:
  enum class UtilType {
    Creation,
    Drop,
  };
  GUtilPlan(GVirtualNetwork* vn, GStorageEngine* store, GCreateStmt* ast);
  GUtilPlan(GVirtualNetwork* vn, GStorageEngine* store, GDropStmt* ast);
  virtual int execute(gqlite_callback);

private:
private:
  UtilType _type;
  std::variant<std::string> _var;
};