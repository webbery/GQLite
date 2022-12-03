#pragma once
#include <stdint.h>

#ifdef _WIN32
#ifdef gqlite_EXPORTS
#define SYMBOL_EXPORT __declspec(dllexport)
#else
#define SYMBOL_EXPORT
#endif
#else // UNIX
#define SYMBOL_EXPORT __attribute__((visibility("default")))
#endif

#define ECode_Success               0
#define ECode_Fail                  1
#define ECODE_NULL_PTR              2
#define ECode_No_Memory             3
#define ECode_DB_Create_Fail        4
#define ECode_Graph_Not_Exist       5
#define ECode_BD_Wrong_Type         6
#define ECode_GQL_Grammar_Error     7
#define ECode_GQL_Index_Not_Exist   8
#define ECode_GQL_Path_Not_Exist    9
#define ECode_Index_File_Not_Exist  10
#define ECode_Group_Not_Exist       11
#define ECode_GQL_Vertex_Not_Exist  50
#define ECode_GQL_Vertex_Exist      51
#define ECode_GQL_Edge_Exist        100
#define ECode_GQL_Edge_Type_Unknow  101
#define ECode_GQL_Parse_Fail        200
#define ECode_DISK_OPEN_FAIL        300
#define ECode_DB_Drop_Fail          301
#define ECode_DATUM_Not_Exist       400
#define ECode_TRANSTION_Not_Exist   500
#define ECode_Query_Stop            600
#define ECode_Query_Pause           601
#define ECode_Remove_Unknow_Type    700

typedef void* gqlite;

typedef struct _gqlite_statement {

}gqlite_statement;

enum gqlite_primitive_type {
  gqlite_int,
  gqlite_string,
  gqlite_decimal,
  gqlite_array,
  gqlite_object
};

typedef enum _gqlite_id_type {
  integer = 1,
  bytes
} gqlite_id_type;

typedef struct _gqlite_vertex {
  // nodeid, C-style string
  union {
    char* cid;
    uint64_t uid;
  };
  gqlite_id_type type;
  // node properties
  char* properties;
  uint32_t len;
}gqlite_vertex;

typedef struct _gqlite_edge {
  gqlite_vertex* from;
  gqlite_vertex* to;
  bool direction;
  char* properties;
  uint32_t len;
}gqlite_edge;

typedef enum _gqlite_node_type {
  gqlite_node_type_vertex,
  gqlite_node_type_edge,
}gqlite_node_type;

typedef struct _gqlite_node {
  gqlite_node_type _type;
  union {
    gqlite_vertex* _vertex;
    gqlite_edge* _edge;
  };
  _gqlite_node* _next;
}gqlite_node;

enum gqlite_result_type {
  gqlite_result_type_node,
  gqlite_result_type_cmd,
  gqlite_result_type_info
};
typedef struct _gqlite_result {
  union {
    gqlite_node* nodes;
    char** infos;
  };
  uint32_t count;
  gqlite_result_type type;
  int errcode;
}gqlite_result;

#ifdef __cplusplus
extern "C" {
#endif

  SYMBOL_EXPORT int gqlite_open(gqlite** ppDb, const char* filename = nullptr);

  /**
   * @brief get current opened db version
   * @param major
   * @param minor
   * @param patch
   * @return db version.
   *         0 means no database is opened.
  */
  SYMBOL_EXPORT int gqlite_version(gqlite* ppDb, int* major, int* minor, int* patch);
  SYMBOL_EXPORT int gqlite_exec(gqlite* pDb, const char* gql, int (*gqlite_callback)(gqlite_result*, void*), void*, char** err);
  SYMBOL_EXPORT int gqlite_create(gqlite* pDb, const char* gql, gqlite_statement** statement);
  SYMBOL_EXPORT int gqlite_execute(gqlite* pDb, gqlite_statement* statement);
  SYMBOL_EXPORT int gqlite_next(gqlite* pDb, gqlite_statement* statement, gqlite_result** result);

  SYMBOL_EXPORT int gqlite_close(gqlite* pDb);
  SYMBOL_EXPORT char* gqlite_error(gqlite* pDb, int error);
  SYMBOL_EXPORT void gqlite_free(void* ptr);

#ifdef __cplusplus
}
#endif
