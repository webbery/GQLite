#include "base/lang/lang.h"
#include <list>

template <NodeType T> struct GTypeTraits {};

template <> struct GTypeTraits<NodeType::GQLExpression> {
  typedef GGQLExpression type;
};
template <> struct GTypeTraits<NodeType::Literal> {
  typedef GLiteral type;
};

template <> struct GTypeTraits<NodeType::CreationStatement> {
  typedef GCreateStmt type;
};

template <> struct GTypeTraits<NodeType::UpsetStatement> {
  typedef GUpsetStmt type;
};

template <> struct GTypeTraits<NodeType::ArrayExpression> {
  typedef GArrayExpression type;
};

template <> struct GTypeTraits<NodeType::Property> {
  typedef GProperty type;
};

template <> struct GTypeTraits<NodeType::BinaryExpression> {
  typedef GProperty type;
};

template <> struct GTypeTraits<NodeType::QueryStatement> {
  typedef GQueryStmt type;
};

template <> struct GTypeTraits<NodeType::VertexDeclaration> {
  typedef GVertexDeclaration type;
};

template <> struct GTypeTraits<NodeType::EdgeDeclaration> {
  typedef GEdgeDeclaration type;
};

/******************************
 * accept function will visit all nodes start from input node.
 * We define ourself's visitor to process nodes when type is matched.
 * And parameter of path show current tree path of node type.
 ******************************/
template <typename Visitor>
VisitFlow accept(GASTNode* node, Visitor& visitor, std::list<NodeType>& path) {
  if (node == nullptr) return VisitFlow::Children;
  path.push_back(node->_nodetype);
  VisitFlow vf = VisitFlow::Children;
  switch(node->_nodetype) {
    case NodeType::GQLExpression: visitor.apply(node, path); break;
    case NodeType::CreationStatement:
    {
      GTypeTraits<NodeType::CreationStatement>::type* ptr = reinterpret_cast<GTypeTraits<NodeType::CreationStatement>::type*>(node->_value);
      vf = visitor.apply(ptr, path);
      switch(vf) {
        case VisitFlow::Children:
        {
          GASTNode* list = ptr->indexes();
          VisitFlow state = accept(list, visitor, path);
          if (state != VisitFlow::Children) return state;
        }
        break;
        case VisitFlow::Return: vf = VisitFlow::Return;
        default: break;
      }
    }
    break;
    case NodeType::UpsetStatement:
    {
      GTypeTraits<NodeType::UpsetStatement>::type* ptr = reinterpret_cast<GTypeTraits<NodeType::UpsetStatement>::type*>(node->_value);
      vf = visitor.apply(ptr, path);
    }
    break;
    case NodeType::VertexDeclaration:
    {
      GASTNode* ptr = node;
      while (ptr)
      {
        GTypeTraits<NodeType::VertexDeclaration>::type* value = reinterpret_cast<GTypeTraits<NodeType::VertexDeclaration>::type*>(ptr->_value);
        vf = visitor.apply(value, path);
        switch(vf) {
          case VisitFlow::Children:
          {
            accept(value->vertex(), visitor, path);
          }
          break;
          default: break;
        }
        ptr = ptr->_children;
      }
    }
    break;
    case NodeType::Literal:
    {
      GTypeTraits<NodeType::Literal>::type* ptr = reinterpret_cast<GTypeTraits<NodeType::Literal>::type*>(node->_value);
      vf = visitor.apply(ptr, path);
    }
    break;
    case NodeType::ArrayExpression:
    {
      GTypeTraits<NodeType::ArrayExpression>::type* ptr = reinterpret_cast<GTypeTraits<NodeType::ArrayExpression>::type*>(node->_value);
      vf = visitor.apply(ptr, path);
      switch(vf) {
        case VisitFlow::Children:
        {
          auto itr = ptr->begin();
          while(itr != ptr->end()) {
            accept(*itr, visitor, path);
            ++itr;
          }
        }
        break;
        default:
        break;
      }
    }
    break;
    case NodeType::QueryStatement:
    {
      GTypeTraits<NodeType::QueryStatement>::type* ptr = reinterpret_cast<GTypeTraits<NodeType::QueryStatement>::type*>(node->_value);
      vf = visitor.apply(ptr, path);
      accept(ptr->graph(), visitor, path);
      if (ptr->where()) {
        accept(ptr->where(), visitor, path);
      }
    }
    break;
    case NodeType::Property:
    {
      GTypeTraits<NodeType::Property>::type* ptr = reinterpret_cast<GTypeTraits<NodeType::Property>::type*>(node->_value);
      vf = visitor.apply(ptr, path);
      switch (vf)
      {
      case VisitFlow::Children:
        vf = accept(ptr->value(), visitor, path);
        break;
      default:
        break;
      }
    }
    break;
    case NodeType::ObjectExpression:
    {
      vf = visitor.apply((GASTNode*)node->_value, path);
      if (vf == VisitFlow::Children) {
        vf = accept((GASTNode*)node->_value, visitor, path);
      }
    }
    break;
    default: vf = visitor.apply(node, path); break;
  }
  for (size_t idx = 0; idx < node->_size; ++idx) {
    accept(node->_children + idx, visitor, path);
  }
  path.pop_back();
  return vf;
}

/**
 * @brief This is used to visit an AST with custom warpper.
 * 
 * @tparam _Wrap 
 */
class GAST{
public:
  GAST(GASTNode* node) : _root(node) {}

  template <typename Visitor>
  void parse(Visitor& visitor) {
    std::list<NodeType> path;
    accept(_root, visitor, path);
  }

private:
  GASTNode* _root;
};