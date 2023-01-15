%define api.pure full
%locations
%define parse.error verbose
// %define api.prefix {gql_yy}
%param { yyscan_t scanner }
%code requires {
  typedef void* yyscan_t;
}

%code top {
#include <stdio.h>
#include <set>
#include <fmt/format.h>
#include <fmt/printf.h>
#include <fmt/color.h>
#include "Error.h"
#include "Type/Binary.h"
#include "base/lang/lang.h"
#include "base/lang/AST.h"

#define GET_GRAPH(name)  \
  GGraph* pGraph = stm._graph->getGraph(name);\
  if (!pGraph) {\
    break;\
  }
} // top

%code {
#define YY_DECL \
       int yylex(YYSTYPE* yylval_param, YYLTYPE* yylloc_param, void* yyscanner, GVirtualEngine& stm)

void yyerror(YYLTYPE* yyllocp, yyscan_t unused, GVirtualEngine& stm, const char* msg) {
  std::string err_index;
  if (stm._errIndx) {
    std::string err_offset(stm._errIndx - 1, ' ');
    err_index += err_offset + "~";
  }
  // printf("\033[22;0m%s\n%s\n",
  //   stm.gql().c_str(), err_index.c_str());
  fmt::print(fmt::fg(fmt::color::red), "Error:\t{}:\n\t{}\n\t{}\n",
    msg, stm.gql(), err_index);
}

struct GASTNode* INIT_STRING_AST(const char* key) {
  size_t len = strlen(key);
  // void* s = malloc(len);
  // memcpy(s, key, len);
  // printf("|-> %s\n", key);
  GLiteral* str = new GLiteralString(key, len);
  return NewAst(NodeType::Literal, str, nullptr, 0);
}

template<typename T>
struct GASTNode* INIT_NUMBER_AST(T v) {
  GLiteral* number = new GLiteralNumber(v);
  return NewAst(NodeType::Literal, number, nullptr, 0);
}

struct GASTNode* INIT_NUMBER_AST(double v, AttributeKind kind) {
  switch (kind) {
  case AttributeKind::Integer:
    if (v <= std::numeric_limits<int>::max()) {
      return INIT_NUMBER_AST((int)v);
    }
    else if (v <= std::numeric_limits<long>::max()) {
      return INIT_NUMBER_AST((long)v);
    }
    else if (v > 0 && v <= std::numeric_limits<uint64_t>::max()){
      return INIT_NUMBER_AST((uint64_t)v);
    }
    else { // use double
      return INIT_NUMBER_AST(v);
    }
  default:
    return INIT_NUMBER_AST(v);
  }
}

void startScript(struct yyguts_t * yyg);
} // %code

%lex-param {GVirtualEngine& stm}
%parse-param {GVirtualEngine& stm}
%union {
  struct GASTNode* node;
  double __f;
  char var_name[32];
  char* __c;
  size_t __offset;
  time_t __datetime;
  int32_t __int;
  nlohmann::json* __json;
}

%start line_list

%token <var_name> VAR_HASH
%token <__f> VAR_DECIMAL
%token <__c> VAR_BASE64 LITERAL_STRING VAR_NAME LITERAL_PATH
%token <__f> VAR_INTEGER
%token <__datetime> VAR_DATETIME
%token <node> KW_VERTEX KW_EDGE
%token QUOTE STAR
%token KW_AST KW_ID KW_GRAPH KW_COMMIT
%token KW_CREATE KW_DROP KW_IN KW_REMOVE KW_UPSET left_arrow right_arrow KW_BIDIRECT_RELATION KW_REST KW_DELETE
%token OP_QUERY KW_INDEX OP_WHERE OP_GEOMETRY neighbor
%token group dump import
%token CMD_SHOW 
%token OP_GREAT_THAN OP_LESS_THAN OP_GREAT_THAN_EQUAL OP_LESS_THAN_EQUAL equal AND OR OP_NEAR
%token SKIP
%token FUNCTION_ARROW RETURN IF ELSE
%token limit profile property

%type <var_name> a_edge
%type <node> a_graph_expr
%type <node> condition_json normal_json
%type <node> condition_value normal_value number right_value simple_value geometry_condition range_comparable datetime_comparable range_comparable_obj
%type <node> condition_values normal_values simple_values
%type <node> normal_object condition_object
%type <node> condition_array normal_array
%type <node> normal_properties condition_properties
%type <node> where_expr a_walk vertex_start_walk edge_start_walk a_simple_graph condition_vertex a_walk_range
%type <node> function_call function_params function_obj statements statement if_state ret_state expr assign_state
%type <node> normal_property condition_property
%type <node> gql
%type <node> creation dump_graph
%type <node> upset_vertexes vertex_list vertexes vertex
%type <node> a_simple_query query_kind_expr a_match match_expr
%type <node> query_kind
%type <node> a_graph_properties graph_property graph_properties
%type <node> a_value
%type <node> a_group group_list groups vertex_group edge_group
%type <node> drop_graph remove_vertexes remove_edges
%type <node> upset_edges edge_pattern connection a_link_condition
%type <node> links link condition_links condition_link_item condition_link
%type <node> key string_list strings intergers property_list a_vector number_list

%%
line_list: line_list line ';' {}
          | line ';' {}
          | comment {}
          /* | error SEMICOLON {} */
          ;
comment: SKIP{};
line: gql
          {
            stm._errorCode = stm.execAST($1);
            FreeAst($1);
          }
        | utility_cmd { stm._cmdtype = GQL_Util; }
        ;
gql: creation
          {
            GGQLExpression* expr = new GGQLExpression();
            $$ = NewAst(NodeType::GQLExpression, expr, $1, 1);
            stm._cmdtype = GQL_Creation;
          }
        | a_simple_query {
            GGQLExpression* expr = new GGQLExpression();
            $$ = NewAst(NodeType::GQLExpression, expr, $1, 1);
            stm._cmdtype = GQL_Query;
          }
        | upset_vertexes
          {
            GGQLExpression* expr = new GGQLExpression();
            $$ = NewAst(NodeType::GQLExpression, expr, $1, 1);
            stm._cmdtype = GQL_Upset;
          }
        | upset_edges
          {
            GGQLExpression* expr = new GGQLExpression();
            $$ = NewAst(NodeType::GQLExpression, expr, $1, 1);
            stm._cmdtype = GQL_Upset;
          }
        | remove_vertexes { $$ = $1; stm._cmdtype = GQL_Remove; }
        | remove_edges { $$ = $1; stm._cmdtype = GQL_Remove; }
        | drop_graph
          {
            GGQLExpression* expr = new GGQLExpression();
            $$ = NewAst(NodeType::GQLExpression, expr, $1, 1);
            stm._cmdtype = GQL_Drop;
          }
        | dump_graph
          {
            GGQLExpression* expr = new GGQLExpression();
            $$ = NewAst(NodeType::GQLExpression, expr, $1, 1);
            stm._cmdtype = GQL_Util;
          }
        ;
utility_cmd: CMD_SHOW KW_GRAPH
          {
            // std::vector<std::string> vg = stm._graph->getGraphs();
            // gqlite_result results;
            // init_result_info(results, vg);
            // stm._result_callback(&results);
            // release_result_info(results);
            stm._errorCode = ECode_Success;
          }
        | CMD_SHOW KW_GRAPH LITERAL_STRING
          {
            GGQLExpression* expr = new GGQLExpression(GGQLExpression::CMDType::SHOW_GRAPH_DETAIL, $3);
            free($3);
            auto ast = NewAst(NodeType::GQLExpression, expr, nullptr, 0);
            stm._errorCode = stm.execCommand(ast);
            FreeAst(ast);
          }
        | KW_AST gql
          {
            fmt::print("AST:\n");
            // DumpAst($2);
            GViewVisitor visitor;
            std::list<NodeType> ln;
            accept($2, visitor, ln);
            FreeAst($2);
            stm._cmdtype = GQL_Util;
          }
        | profile gql {}
        | import LITERAL_PATH
          {
            free($2);
            stm._cmdtype = GQL_Util;
          }
        ;
creation: '{' KW_CREATE ':' LITERAL_STRING ',' groups '}'
            {
              GCreateStmt* createStmt = new GCreateStmt($4, $6);
              free($4);
              $$ = NewAst(NodeType::CreationStatement, createStmt, nullptr, 0);
              stm._errorCode = ECode_Success;
            }
        | '{' KW_CREATE ':' LITERAL_STRING ',' KW_INDEX ':' function_call '}'
              {
                free($4);
              }
        ;
dump_graph: '{' dump ':' LITERAL_STRING '}'
              {
                GDumpStmt* stmt = new GDumpStmt($4);
                free($4);
                $$ = NewAst(NodeType::DumpStatement, stmt, nullptr, 0);
                stm._errorCode = ECode_Success;
              };
upset_vertexes: '{' KW_UPSET ':' LITERAL_STRING ',' KW_VERTEX ':' vertex_list '}'
              {
                GUpsetStmt* upsetStmt = new GUpsetStmt($4, $8);
                free($4);
                $$ = NewAst(NodeType::UpsetStatement, upsetStmt, nullptr, 0);
              }
        | '{' KW_UPSET ':' LITERAL_STRING ',' property ':' normal_json ',' where_expr '}'
              {
                GUpsetStmt* upsetStmt = new GUpsetStmt($4, $8, $10);
                free($4);
                $$ = NewAst(NodeType::UpsetStatement, upsetStmt, nullptr, 0);
              }
        | error '}'
              {
                fmt::print(fmt::fg(fmt::color::red), "Error:\t{}\n",
                  "should you use upset edge? input format is {upset: [vertex, edge, vertex], edge: ...}\n");
                yyerrok;
                stm._errorCode = GQL_GRAMMAR_OBJ_FAIL;
                YYABORT;
              };
remove_vertexes: '{' KW_REMOVE ':' LITERAL_STRING ',' KW_VERTEX ':' condition_object '}'
              {
                GRemoveStmt* rmStmt = new GVertexRemoveStmt($4, $8);
                free($4);
                $$ = NewAst(NodeType::RemoveStatement, rmStmt, nullptr, 0);
              };
remove_edges: '{' KW_REMOVE ':' LITERAL_STRING ',' KW_EDGE ':' condition_links '}'
              {
                GRemoveStmt* rmStmt = new GEdgeRemoveStmt($4, $8);
                free($4);
                $$ = NewAst(NodeType::RemoveStatement, rmStmt, nullptr, 0);
              };
upset_edges: '{' KW_UPSET ':' LITERAL_STRING ',' KW_EDGE ':' link '}'
              {
                GUpsetStmt* upsetStmt = new GUpsetStmt($4, $8);
                free($4);
                $$ = NewAst(NodeType::UpsetStatement, upsetStmt, nullptr, 0);
              }
        | '{' KW_UPSET ':' LITERAL_STRING ',' KW_EDGE ':' '[' links ']' '}'
              {
                GUpsetStmt* upsetStmt = new GUpsetStmt($4, $9);
                free($4);
                $$ = NewAst(NodeType::UpsetStatement, upsetStmt, nullptr, 0);
              };
drop_graph: '{' KW_DROP ':' LITERAL_STRING '}'
              {
                GDropStmt* dropStmt = new GDropStmt($4);
                free($4);
                $$ = NewAst(NodeType::DropStatement, dropStmt, nullptr, 0);
              };
groups: group ':' '[' group_list ']'
              {
                $$ = $4;
              };
group_list: a_group
              {
                GArrayExpression* array = new GArrayExpression();
                array->addElement($1);
                $$ = NewAst(NodeType::ArrayExpression, array, nullptr, 0);
              }
        | group_list ',' a_group
              {
                GArrayExpression* array = (GArrayExpression*)$1->_value;
                array->addElement($3);
                $$ = $1;
              };
a_group: vertex_group
              {
                $$ = $1;
              }
        | edge_group
              {
                
              }
        ;
vertex_group: LITERAL_STRING
              {
                GGroupStmt* stmt = new GVertexGroupStmt($1);
                free($1);
                $$ = NewAst(NodeType::GroupStatement, stmt, nullptr, 0);
              }
        | '{' VAR_NAME ':' string_list '}'
              {
                GGroupStmt* stmt = new GVertexGroupStmt($2, $4);
                free($2);
                $$ = NewAst(NodeType::GroupStatement, stmt, nullptr, 0);
              }
        | '{' VAR_NAME ':' string_list ',' KW_INDEX ':' string_list '}'
              {
                GGroupStmt* stmt = new GVertexGroupStmt($2, $4, $8);
                free($2);
                $$ = NewAst(NodeType::GroupStatement, stmt, nullptr, 0);
              }
        ;
edge_group: '[' LITERAL_STRING ',' '{' VAR_NAME ':' string_list '}' ',' LITERAL_STRING ']'
              {
                GGroupStmt* stmt = new GEdgeGroupStmt($5, $7, $2, $10);
                free($2);
                free($5);
                free($10);
                $$ = NewAst(NodeType::GroupStatement, stmt, nullptr, 0);
              }
        | '[' LITERAL_STRING ',' LITERAL_STRING ',' LITERAL_STRING ']'
              {
                GGroupStmt* stmt = new GEdgeGroupStmt($4, nullptr, $2, $6);
                free($2);
                free($4);
                free($6);
                $$ = NewAst(NodeType::GroupStatement, stmt, nullptr, 0);
              }
        ;
a_simple_query: 
           '{' query_kind '}'
                {
                  GQueryStmt* queryStmt = new GQueryStmt($2, nullptr, nullptr);
                  $$ = NewAst(NodeType::QueryStatement, queryStmt, nullptr, 0);
                  stm._errorCode = ECode_Success;
                }
        |  '{' query_kind ',' a_graph_expr '}'
                {
                  GQueryStmt* queryStmt = new GQueryStmt($2, $4, nullptr);
                  $$ = NewAst(NodeType::QueryStatement, queryStmt, nullptr, 0);
                  stm._errorCode = ECode_Success;
                }
        | '{' query_kind ',' a_graph_expr ',' where_expr '}'
                {
                  GQueryStmt* queryStmt = new GQueryStmt($2, $4, $6);
                  $$ = NewAst(NodeType::QueryStatement, queryStmt, nullptr, 0);
                  stm._errorCode = ECode_Success;
                };
a_simple_graph: a_walk_range
                {
                  $$ = $1;
                }
        | a_simple_graph ',' a_walk_range
                {
                  // $1->addElement($4);
                  // $$ = $1;
                };
a_walk_range: '[' a_walk ']' { $$ = $2; };
a_walk: vertex_start_walk
                {
                  $$ = $1;
                }
        | vertex_start_walk ',' condition_vertex
                {
                  ((GWalkDeclaration*)($1->_value))->add($3, true);
                }
        | edge_start_walk
                {
                  $$ = $1;
                }
        | edge_start_walk ',' condition_vertex
                {
                  // $1->add($3, )
                }
        | a_walk ',' STAR
                {
                  $$ = $1;
                };
vertex_start_walk: condition_vertex ',' connection
                {
                  GWalkDeclaration* walkDecl = new GWalkDeclaration();
                  walkDecl->add($1, true);
                  walkDecl->add($3, false);
                  $$ = NewAst(NodeType::WalkDeclaration, walkDecl, nullptr, 0);
                }
        | vertex_start_walk ',' condition_vertex ',' connection
                {
                  ((GWalkDeclaration*)($1->_value))->add($3, true);
                  ((GWalkDeclaration*)($1->_value))->add($5, false);
                  $$ = $1;
                };
edge_start_walk: connection ',' condition_vertex
                {
                  GWalkDeclaration* walkDecl = new GWalkDeclaration();
                  walkDecl->add($1, true);
                  walkDecl->add($3, false);
                  $$ = NewAst(NodeType::WalkDeclaration, walkDecl, nullptr, 0);
                }
        | edge_start_walk ',' connection ',' condition_vertex {};
condition_vertex: condition_object { $$ = $1; }
        | key { $$ = $1; };
        | STAR { $$ = INIT_STRING_AST("*"); };
query_kind: OP_QUERY ':' query_kind_expr { $$ = $3; }
        |   OP_QUERY ':' match_expr { $$ = $3; };
query_kind_expr: 
          KW_EDGE { $$ = INIT_STRING_AST("edge"); }
        |  LITERAL_STRING { $$ = INIT_STRING_AST($1); free($1); }
        | a_graph_properties { $$ = $1; };
match_expr: //{->: 'alias'}
          '{' a_simple_graph '}' { $$ = $2; };
a_graph_expr:
          KW_IN ':' LITERAL_STRING
                {
                  $$ = INIT_STRING_AST($3);
                  free($3);
                };
where_expr: OP_WHERE ':' a_match { $$ = $3; };
a_match:  condition_vertex { $$ = $1; }
        | a_walk_range { $$ = $1; }
        | '[' a_simple_graph ']' { $$ = $2; }
        ;
string_list: LITERAL_STRING
                {
                  GArrayExpression* array = new GArrayExpression();
                  array->addElement(INIT_STRING_AST($1));
                  free($1);
                  $$ = NewAst(NodeType::ArrayExpression, array, nullptr, 0);
                }
        | '[' strings ']'
              {
                $$ = $2;
              }
        | normal_property {};
// property_list: STAR { $$ = nullptr; }
//         | string_list { $$ = $1; };
strings:  LITERAL_STRING
                {
                  GArrayExpression* array = new GArrayExpression();
                  array->addElement(INIT_STRING_AST($1));
                  free($1);
                  $$ = NewAst(NodeType::ArrayExpression, array, nullptr, 0);
                }
        | strings ',' LITERAL_STRING
              {
                GArrayExpression* array = (GArrayExpression*)$1->_value;
                array->addElement(INIT_STRING_AST($3));
                free($3);
                $$ = $1;
              };
intergers: VAR_INTEGER
              {
                GArrayExpression* array = new GArrayExpression();
                array->addElement(INIT_NUMBER_AST($1, AttributeKind::Integer));
                $$ = NewAst(NodeType::ArrayExpression, array, nullptr, 0);
              }
        | intergers ',' VAR_INTEGER
              {
                GArrayExpression* array = (GArrayExpression*)$1->_value;
                array->addElement(INIT_NUMBER_AST($3, AttributeKind::Integer));
                $$ = $1;
              };
number: VAR_DECIMAL { $$ = INIT_NUMBER_AST($1, AttributeKind::Number); }
        | VAR_INTEGER { $$ = INIT_NUMBER_AST($1, AttributeKind::Integer); };
a_graph_properties:
          graph_property { $$ = $1; }
        | '[' graph_properties ']' { $$ = $2; }
        | error ']'
          {
            fmt::print(fmt::fg(fmt::color::red), "Error:\tinput object is not a correct json:\n");
            yyerrok;
            stm._errorCode = GQL_GRAMMAR_OBJ_FAIL;
            YYABORT;
          }
        ;
graph_properties:
          graph_property
              {
                GArrayExpression* array = new GArrayExpression();
                array->addElement($1);
                $$ = NewAst(NodeType::ArrayExpression, array, nullptr, 0);
              }
        | graph_properties ',' graph_property
              {
                GArrayExpression* array = (GArrayExpression*)$1->_value;
                array->addElement($3);
                $$ = $1;
              };
graph_property:
          KW_VERTEX '.' VAR_NAME
              {
                GMemberExpression* expr = new GMemberExpression(INIT_STRING_AST("vertex"), INIT_STRING_AST($3));
                $$ = NewAst(NodeType::MemberExpression, expr, nullptr, 0);
                free($3);
              }
        | KW_EDGE '.' VAR_NAME
              {
                GMemberExpression* expr = new GMemberExpression(INIT_STRING_AST("edge"), INIT_STRING_AST($3));
                $$ = NewAst(NodeType::MemberExpression, expr, nullptr, 0);
                free($3);
              }
        |  VAR_NAME '.' VAR_NAME
              {
                GMemberExpression* expr = new GMemberExpression(INIT_STRING_AST($1), INIT_STRING_AST($3));
                $$ = NewAst(NodeType::MemberExpression, expr, nullptr, 0);
                free($3);
                free($1);
              }
        | KW_VERTEX '.' function_call {}
        | KW_EDGE '.' function_call {}
        | VAR_NAME '.' function_call
              {
                auto scope = INIT_STRING_AST($1);
                $$ = NewAst(NodeType::VariableDeclarator, scope, $3, 1);
                free($1);
              };
vertex_list: '[' vertexes ']'
              {
                $$ = $2;
              };
vertexes: vertex
              {
                GArrayExpression* vertexes = new GArrayExpression();
                vertexes->addElement($1);
                $$ = NewAst(NodeType::ArrayExpression, vertexes, nullptr, 0);
              }
        | vertexes ',' vertex
              {
                GArrayExpression* vertexes = (GArrayExpression*)$1->_value;
                vertexes->addElement($3);
                $$ = $1;
              };
vertex: '[' LITERAL_STRING ',' normal_json ']'
              {
                GVertexDeclaration* decl = new GVertexDeclaration(INIT_STRING_AST($2), $4);
                free($2);
                $$ = NewAst(NodeType::VertexDeclaration, decl, nullptr, 0);
              }
        | '[' VAR_INTEGER ']'
              {
                GVertexDeclaration* decl = new GVertexDeclaration(INIT_NUMBER_AST($2, AttributeKind::Integer), nullptr);
                $$ = NewAst(NodeType::VertexDeclaration, decl, nullptr, 0);
              }
        | '[' VAR_INTEGER ',' normal_json ']'
              {
                GVertexDeclaration* decl = new GVertexDeclaration(INIT_NUMBER_AST($2, AttributeKind::Integer), $4);
                $$ = NewAst(NodeType::VertexDeclaration, decl, nullptr, 0);
              }
        | key
              {
                GVertexDeclaration* decl = new GVertexDeclaration($1, nullptr);
                $$ = NewAst(NodeType::VertexDeclaration, decl, nullptr, 0);
              };
condition_links: condition_link
              {
                GArrayExpression* edges = new GArrayExpression();
                edges->addElement($1);
                $$ = NewAst(NodeType::ArrayExpression, edges, nullptr, 0);
              }
        | condition_links ',' condition_link
              {
                GArrayExpression* edges = (GArrayExpression*)$1->_value;
                edges->addElement($3);
                $$ = $1;
              }
        ;
condition_link: '[' condition_link_item ',' connection ',' condition_link_item ']'
              {
                GEdgeDeclaration* edge = new GEdgeDeclaration($4, $2, $6);
                $$ = NewAst(NodeType::EdgeDeclaration, edge, nullptr, 0);
              };
condition_link_item: VAR_INTEGER {$$ = INIT_NUMBER_AST($1, AttributeKind::Integer);}
        | LITERAL_STRING
              {
                $$ = INIT_STRING_AST($1);
                free($1);
              }
        | STAR { $$ = INIT_STRING_AST("*"); }
        ;
links: link
              {
                GArrayExpression* edges = new GArrayExpression();
                edges->addElement($1);
                $$ = NewAst(NodeType::ArrayExpression, edges, nullptr, 0);
              }
        | links ',' link
              {
                GArrayExpression* edges = (GArrayExpression*)$1->_value;
                edges->addElement($3);
                $$ = $1;
              };
link: '[' key ',' connection ',' key ']'
              {
                GEdgeDeclaration* edge = new GEdgeDeclaration($4, $2, $6);
                $$ = NewAst(NodeType::EdgeDeclaration, edge, nullptr, 0);
              }
        | '[' key ']'
              {
                GEdgeDeclaration* edge = new GEdgeDeclaration("--", $2);
                $$ = NewAst(NodeType::EdgeDeclaration, edge, nullptr, 0);
              }
        ;
multi_link: '[' key ',' a_edge ',' '[' strings ']' ']'
              {
                // GEdgeDeclaration* edge = new GEdgeDeclaration($4, $2, $6);
              }
        | '[' key ',' a_edge ',' '[' intergers ']' ']'
              {};
connection: a_link_condition { $$ = $1;}
        | a_edge  { $$ = INIT_STRING_AST($1); };
a_vector: '[' number_list ']' { $$ = $2; };
number_list: number
              {
                GArrayExpression* elemts = new GArrayExpression();
                elemts->addElement($1);
                $$ = NewAst(NodeType::ArrayExpression, elemts, nullptr, 0);
              }
        | number_list ',' number
              {
                GArrayExpression* elemts = (GArrayExpression*)$1->_value;
                elemts->addElement($3);
                $$ = $1;
              };
normal_json: normal_value { $$ = $1; };
normal_value: normal_object { $$ = $1; }
        | normal_array { $$ = $1; };
right_value: condition_object { $$ = $1; }
        | condition_array { $$ = $1; }
        | simple_value { $$ = $1; };
simple_value: VAR_BASE64
              {
                GLiteralBinary* bin = new GLiteralBinary($1, "b64");
                free($1);
                $$ = NewAst(NodeType::Literal, bin, nullptr, 0);
              }
        | LITERAL_STRING
              {
                $$ = INIT_STRING_AST($1);
                free($1);
              }
        | VAR_DATETIME
              {
                GLiteralDatetime* dt = new GLiteralDatetime($1);
                $$ = NewAst(NodeType::Literal, dt, nullptr, 0);
              }
        | number
              {
                $$ = $1;
              };
normal_object: '{' normal_properties '}'
            {
              $$ = NewAst(NodeType::ObjectExpression, $2, nullptr, 0);
            };
condition_object: '{' condition_properties '}'
            {
              $$ = NewAst(NodeType::ObjectExpression, $2, nullptr, 0);
            }
        | error '}'
            {
              fmt::print(fmt::fg(fmt::color::red), "Error:\tinput object is not a correct property\n");
              yyerrok;
              stm._errorCode = GQL_GRAMMAR_OBJ_FAIL;
              YYABORT;
            }
        ;
normal_properties: normal_property {
              GArrayExpression* props = new GArrayExpression();
              props->addElement($1);
              $$ = NewAst(NodeType::ArrayExpression, props, nullptr, 0);
            }
        | normal_properties ',' normal_property
              {
                GArrayExpression* props = (GArrayExpression*)$1->_value;
                props->addElement($3);
                $$ = $1;
              };
condition_properties: condition_property
              {
                GArrayExpression* props = new GArrayExpression();
                props->addElement($1);
                $$ = NewAst(NodeType::ArrayExpression, props, nullptr, 0);
              }
        | condition_properties ',' condition_property
              {
                GArrayExpression* props = (GArrayExpression*)$1->_value;
                props->addElement($3);
                $$ = $1;
              };
normal_property: VAR_NAME ':' simple_value
              {
                GProperty* prop = new GProperty($1, $3);
                free($1);
                $$ = NewAst(NodeType::Property, prop, nullptr, 0);
              }
        | VAR_NAME ':' normal_value
              {
                GProperty* prop = new GProperty($1, $3);
                free($1);
                $$ = NewAst(NodeType::Property, prop, nullptr, 0);
              };
condition_property: VAR_NAME ':' right_value
              {
                GProperty* prop = new GProperty($1, $3);
                free($1);
                $$ = NewAst(NodeType::Property, prop, nullptr, 0);
              }
        | VAR_NAME ':' STAR
              {
                GProperty* prop = new GProperty($1, INIT_STRING_AST("*"));
                free($1);
                $$ = NewAst(NodeType::Property, prop, nullptr, 0);
              }
        | datetime_comparable { $$ = $1;}
        | range_comparable { $$ = $1;}
        | KW_ID ':' key
              {
                GProperty* prop = new GProperty("id", $3);
                $$ = NewAst(NodeType::Property, prop, nullptr, 0);
              }
        | AND ':' condition_array
              {
                GProperty* prop = new GProperty("and", $3);
                $$ = NewAst(NodeType::BinaryExpression, prop, nullptr, 0);
              }
        | OR ':' condition_array
              {
                GProperty* prop = new GProperty("or", $3);
                $$ = NewAst(NodeType::BinaryExpression, prop, nullptr, 0);
              }
        | group ':' LITERAL_STRING
              {
                struct GASTNode* value = INIT_STRING_AST($3);
                free($3);
                GProperty* prop = new GProperty("group", value);
                $$ = NewAst(NodeType::Property, prop, nullptr, 0);
              }
        | OP_NEAR ':' '{' geometry_condition '}'
              {
                GObjectFunction* obj = (GObjectFunction*)($4->_value);
                obj->setFunctionName("__near__", "__global__");
                GProperty* prop = new GProperty("near", $4);
                $$ = NewAst(NodeType::BinaryExpression, prop, nullptr, 0);
              };
range_comparable: OP_GREAT_THAN_EQUAL ':' range_comparable_obj
              {
                GProperty* prop = new GProperty("gte", $3);
                $$ = NewAst(NodeType::BinaryExpression, prop, nullptr, 0);
              }
        | OP_LESS_THAN_EQUAL ':' range_comparable_obj
              {
                GProperty* prop = new GProperty("lte", $3);
                $$ = NewAst(NodeType::BinaryExpression, prop, nullptr, 0);
              }
        | OP_GREAT_THAN ':' range_comparable_obj
              {
                GProperty* prop = new GProperty("gt", $3);
                $$ = NewAst(NodeType::BinaryExpression, prop, nullptr, 0);
              }
        | OP_LESS_THAN ':' range_comparable_obj
              {
                GProperty* prop = new GProperty("lt", $3);
                $$ = NewAst(NodeType::BinaryExpression, prop, nullptr, 0);
              };
range_comparable_obj: number { $$ = $1; }
        | function_obj { $$ = $1; };
datetime_comparable: OP_GREAT_THAN_EQUAL ':' VAR_DATETIME
              {
                GLiteralDatetime* dt = new GLiteralDatetime($3);
                GProperty* prop = new GProperty("gte", NewAst(NodeType::Literal, dt, nullptr, 0));
                $$ = NewAst(NodeType::BinaryExpression, prop, nullptr, 0);
              }
        | OP_LESS_THAN_EQUAL ':' VAR_DATETIME
              {
                GLiteralDatetime* dt = new GLiteralDatetime($3);
                GProperty* prop = new GProperty("lte", NewAst(NodeType::Literal, dt, nullptr, 0));
                $$ = NewAst(NodeType::BinaryExpression, prop, nullptr, 0);
              }
        | OP_GREAT_THAN ':' VAR_DATETIME
              {
                GLiteralDatetime* dt = new GLiteralDatetime($3);
                GProperty* prop = new GProperty("gt", NewAst(NodeType::Literal, dt, nullptr, 0));
                $$ = NewAst(NodeType::BinaryExpression, prop, nullptr, 0);
              }
        | OP_LESS_THAN ':' VAR_DATETIME
              {
                GLiteralDatetime* dt = new GLiteralDatetime($3);
                GProperty* prop = new GProperty("lt", NewAst(NodeType::Literal, dt, nullptr, 0));
                $$ = NewAst(NodeType::BinaryExpression, prop, nullptr, 0);
              };
geometry_condition: OP_GEOMETRY ':' a_vector ',' range_comparable
              {
                GObjectFunction* obj = new GObjectFunction();
                obj->addFunctionParam($3);
                obj->addFunctionParam($5);
                $$ = NewAst(NodeType::CallExpression, obj, nullptr, 0);
              }
        | range_comparable ',' OP_GEOMETRY ':' a_vector
              {
                GObjectFunction* obj = new GObjectFunction();
                obj->addFunctionParam($5);
                obj->addFunctionParam($1);
                $$ = NewAst(NodeType::CallExpression, obj, nullptr, 0);
              };
condition_array: '[' ']' { $$ = nullptr; }
        | '[' condition_values ']'
              {
                $$ = $2;
              }
        ;
normal_array:    '[' ']' { $$ = nullptr; }
        | '[' normal_values ']'
              {
                $$ = $2;
              }
        | '[' simple_values ']'
              {
                $$ = $2;
              }
        | error ']'
              {
                printf("\033[22;31mDetail:\t%s:\033[22;0m\n",
                  "array is not a correct array");
                stm._errorCode = GQL_GRAMMAR_ARRAY_FAIL;
                $$ = nullptr;
              }
        ;
normal_values: normal_value
              {
                GArrayExpression* values = new GArrayExpression();
                values->addElement($1);
                $$ = NewAst(NodeType::ArrayExpression, values, nullptr, 0);
              }
        | normal_values ',' normal_value
              {
                GArrayExpression* values = (GArrayExpression*)$1->_value;
                values->addElement($3);
                $$ = $1;
              };
simple_values: simple_value
              {
                GArrayExpression* values = new GArrayExpression();
                values->addElement($1);
                $$ = NewAst(NodeType::ArrayExpression, values, nullptr, 0);
              }
        | simple_values ',' simple_value
              {
                GArrayExpression* values = (GArrayExpression*)$1->_value;
                values->addElement($3);
                $$ = $1;
              };
condition_values: right_value {
                GArrayExpression* values = new GArrayExpression();
                values->addElement($1);
                $$ = NewAst(NodeType::ArrayExpression, values, nullptr, 0);
              }
        | condition_values ',' right_value
              {
                GArrayExpression* values = (GArrayExpression*)$1->_value;
                values->addElement($3);
                $$ = $1;
              }
        | condition_values ',' KW_REST {};
a_link_condition: a_edge ':' a_value
              {
                GEdgeDeclaration* edge = new GEdgeDeclaration($1, $3);
                $$ = NewAst(NodeType::EdgeDeclaration, edge, nullptr, 0);
              }
        | a_edge ':' normal_json
              {
                GEdgeDeclaration* edge = new GEdgeDeclaration($1, $3);
                $$ = NewAst(NodeType::EdgeDeclaration, edge, nullptr, 0);
              }
        | a_edge ':' function_call
              {
                GEdgeDeclaration* edge = new GEdgeDeclaration($1, $3);
                $$ = NewAst(NodeType::EdgeDeclaration, edge, nullptr, 0);
              }
        | a_edge ':' KW_REST
              {
                GEdgeDeclaration* edge = new GEdgeDeclaration($1, INIT_STRING_AST("..."));
                $$ = NewAst(NodeType::EdgeDeclaration, edge, nullptr, 0);
              };
a_edge:   right_arrow { memcpy(&$$, "->", 3);}
        | left_arrow { memcpy(&$$, "<-", 3); }
        | KW_BIDIRECT_RELATION { memcpy(&$$, "--", 3); };
a_value:  LITERAL_STRING
              {
                $$ = INIT_STRING_AST($1);
                free($1);
              }
        | VAR_DECIMAL { $$ = INIT_NUMBER_AST($1, AttributeKind::Number); }
        | VAR_INTEGER { $$ = INIT_NUMBER_AST($1, AttributeKind::Integer); };
key: VAR_INTEGER { $$ = INIT_NUMBER_AST($1, AttributeKind::Integer); }
        | LITERAL_STRING { $$ = INIT_STRING_AST($1); free($1);};
function_call: VAR_NAME function_params
              {
                auto fname = INIT_STRING_AST($1);
                free($1);
                if ($2 == nullptr) {
                  $$ = NewAst(NodeType::CallExpression, fname, $2, 0);
                }
                else {
                  $$ = NewAst(NodeType::CallExpression, fname, $2, 1);
                }
              };
function_obj: function_params FUNCTION_ARROW '{' statements '}'
              {
                $$ = NewAst(NodeType::BlockStatement, $1, $4, 0);
              };
function_params
        : '(' ')' { $$ = nullptr; }
        | '(' STAR ')' { $$ = INIT_STRING_AST("*"); }
        | '(' string_list ')' { $$ = $2; }
        ;
statements: statement { $$ = $1;}
        | statements statement { $$ = $1;};
statement
        : if_state        { $$ = $1;}
        | ret_state ';'   { $$ = $1;}
        | expr ';'        { $$ = $1;}
        | assign_state ';'{ $$ = $1;}
        ;
if_state: IF expr {};
ret_state
        : RETURN expr { $$ = $2; };
assign_state: {};
exprs: {};
expr: a_value { printf("expr\n"); $$ = $1; };
%%
