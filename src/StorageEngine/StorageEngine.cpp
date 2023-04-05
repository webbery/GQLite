#include "StorageEngine.h"
#include <cassert>
#include <cstdint>
#include <limits>
#include <regex>
#include <stdio.h>
#include <atomic>
#include <utility>
#include "Graph/EntityNode.h"
#include "Graph/EntityEdge.h"
#include "base/Variant.h"
#include "base/type.h"
#include "gqlite.h"
#include "gutil.h"
#include "mdbx.h"
#include "mdbx.h++"

// #ifdef WIN32
// #pragma comment(lib, BINARY_DIR "/" CMAKE_INTDIR "/zstd_static.lib")
// #endif

#if __cplusplus > 201700
#include <filesystem>
using namespace std;
#elif __cplusplus > 201300
#include <experimental/filesystem>
using namespace std::experimental;
#elif __cplusplus > 201103 
#endif

#define GRAPH_EXCEPTION_CATCH(expr) try{\
  expr;\
}catch(std::exception& err) {}
#define CHECK_RESULT(expr) {int ret = ECode_Success; if ((ret = expr) != ECode_Success) return ret;}
#define DB_SCHEMA   "gql_schema"
#define SCHEMA_BASIC  "basic"

using namespace mdbx;
namespace {
  mdbx::slice get(mdbx::txn_managed& txn, mdbx::map_handle& map, const std::string& key)
  {
    mdbx::slice k(key.data(), key.size());
    mdbx::slice absent;
    return txn.get(map, k, absent);
  }

  mdbx::slice get(mdbx::txn_managed& txn, mdbx::map_handle& map, uint64_t key)
  {
    mdbx::slice k(&key, sizeof(uint64_t));
    mdbx::slice absent;
    return txn.get(map, k, absent);
  }

  //mdbx::slice get(mdbx::txn_managed& txn, mdbx::map_handle& map, uint64_t from, uint64_t to) {
  //  assert(to >= from);
  //  mdbx::slice f(&from, sizeof(uint64_t));
  //  mdbx::slice t(&to, sizeof(uint64_t));
  //  mdbx::slice absent;
    //txn.get_equal_or_great(map, f, )
  //}

  int put(mdbx::txn_managed& txn, mdbx::map_handle& map, const std::string& key, mdbx::slice value)
  {
    mdbx::slice k(key.data(), key.size());
    return txn.put(map, k, &value, MDBX_put_flags_t(mdbx::upsert));
  }

  int put(mdbx::txn_managed& txn, mdbx::map_handle& map, uint64_t key, mdbx::slice value)
  {
    mdbx::slice k(&key, sizeof(uint64_t));
    return txn.put(map, k, &value, MDBX_put_flags_t(mdbx::upsert));
  }

  int del(mdbx::txn_managed& txn, mdbx::map_handle& map, uint64_t key) {
    mdbx::slice k(&key, sizeof(uint64_t));
    if (txn.erase(map, k)) return 0;
    return -1;
  }

  int del(mdbx::txn_managed& txn, mdbx::map_handle& map, const std::string& key) {
    mdbx::slice k(key.data(), key.size());
    if (txn.erase(map, k)) return 0;
    return -1;
  }

}

GStorageEngine::GStorageEngine() noexcept
{
}

GStorageEngine::~GStorageEngine() {
  close();
}

int GStorageEngine::open(const char* filename, StoreOption option) {
  if (_env) {
    this->close();
  }
  env::geometry db_geometry;
  env_managed::create_parameters create_param;
  create_param.geometry=db_geometry;

  env::operate_parameters operator_param;
#define DEFAULT_MAX_PROPS  64
  operator_param.max_maps = DEFAULT_MAX_PROPS;
  if (option.mode == ReadWriteOption::read_only) {
    operator_param.mode = env::mode::readonly;
  }
  filesystem::path p(filename);
  if (p.is_relative()) {
    if (!option.directory.empty()) {
      p = filesystem::path(option.directory) / filename;
    }
  }
  std::string fullpath = p.string();
  if (p.has_parent_path() && !filesystem::exists(p.parent_path())) {
    gql::create_directories(
#if defined(__APPLE__) || defined(UNIX) || defined(__linux__)
      p.parent_path().c_str()
#elif defined(WIN32)
      gql::string2wstring(fullpath.c_str())
#endif
    );
  }

#if defined(__APPLE__) || defined(__gnu_linux__) || defined(__linux__) 
  _env = env_managed(fullpath, create_param, operator_param);
#else
  _env = env_managed(gql::string2wstring(fullpath), create_param, operator_param);
#endif
  int ret = startTrans(option.mode);
  mdbx::map_handle handle = openSchema(option.mode);
  thread_local auto id = std::this_thread::get_id();
  mdbx::slice data = ::get(_txns[id], handle, SCHEMA_BASIC);
  if (data.size()) {
    std::vector<uint8_t> v(data.byte_ptr(), data.byte_ptr() + data.size());
    _schema = nlohmann::json::from_cbor(v);
  }
  _schema[SCHEMA_GRAPH_NAME] = p.filename();
  _curDBPath = fullpath;
  initMap(option);
  initDict(option.compress);
  return ret;
}

bool GStorageEngine::isOpen()
{
  if (_schema.is_null() || _schema.empty()) return false;
  return true;
}

void GStorageEngine::close()
{
  thread_local auto id = std::this_thread::get_id();

  if (_txns.count(id)){
    try {
      auto flag = _txns[id].flags();
      if ((flag & MDBX_TXN_RDONLY) == 0) {
        if (!_schema.empty()) {
          mdbx::map_handle handle = openSchema(ReadWriteOption::read_write);
          std::vector<uint8_t> v = nlohmann::json::to_cbor(_schema);
          mdbx::slice data(v.data(), v.size());
          ::put(_txns[id], handle, SCHEMA_BASIC, data);
          // _env.close_map(handle);
        }
        _txns[id].commit();
      }
    } catch (const mdbx::exception& err) {
      printf("err: %s\n", err.what());
    }
  }
  _txns.clear();
  releaseDict();
  if (_env) _env.close();
}

mdbx::map_handle GStorageEngine::openSchema(ReadWriteOption option) {
  mdbx::map_handle schema;
  thread_local auto id = std::this_thread::get_id();
  if (option == ReadWriteOption::read_only) {
    schema = _txns[id].open_map(DB_SCHEMA, mdbx::key_mode::usual, mdbx::value_mode::single);
  } else {
    GRAPH_EXCEPTION_CATCH(schema = _txns[id].create_map(DB_SCHEMA, mdbx::key_mode::usual, mdbx::value_mode::single));
  }
  return schema;
}

void GStorageEngine::initMap(StoreOption option)
{
  addMap(MAP_BASIC, KeyType::Uninitialize);
}

std::string GStorageEngine::getPath() const {
  return _curDBPath;
}

std::string GStorageEngine::getGroupName(group_t gid) const {
  return _groupsName.at(gid);
}

group_t GStorageEngine::getGroupID(const std::string& name) const {
  return _groupsMap.at(name);
}


void GStorageEngine::initDict(int compressLvl)
{
  if (compressLvl > 3 || compressLvl <= 0) compressLvl = 1;
  if (_schema[SCHEMA_GLOBAL][GLOBAL_COMPRESS_LEVEL].empty()) {
    _schema[SCHEMA_GLOBAL][GLOBAL_COMPRESS_LEVEL] = compressLvl;
  }
  else {
    compressLvl = _schema[SCHEMA_GLOBAL][GLOBAL_COMPRESS_LEVEL];
  }
  switch(compressLvl) {
    case 3: // reserved
    case 2: // use multiple compress for data, such as RLE/XOR/Delta/Zig-zag/Snappy/Simple8b
    case 1: // compress only for json-liked data's key
    if (!_schema[SCHEMA_GLOBAL][GLOBAL_COMPRESS_DICT].empty()) {
      _id2key = _schema[SCHEMA_GLOBAL][GLOBAL_COMPRESS_DICT];
      for (auto& item: _id2key) {
        _key2id[item.second] = item.first;
      }
    }
    default: break;
  }
  
  if (_schema[SCHEMA_GLOBAL][GLOBAL_GQL_VERSION].empty()) {
    _schema[SCHEMA_GLOBAL][GLOBAL_GQL_VERSION] = GQL_VERSION;
  }
}

void GStorageEngine::releaseDict()
{
  if (_id2key.size()) {
    _schema[SCHEMA_GLOBAL][GLOBAL_COMPRESS_DICT] = _id2key;
  }
}

void GStorageEngine::addMap(const std::string& prop, KeyType type) {
  if (!isMapExist(prop)) {
    _schema[SCHEMA_CLASS][prop][SCHEMA_CLASS_KEY] = type;
  }
  if (!_groupsMap.count(prop)) {
    _groupsMap[prop] = _groupsName.size() + 1;
    _groupsName[_groupsName.size() + 1] = prop;
  }
}

void GStorageEngine::addIndex(const std::string& indexname)
{
  if (!isIndexExist(indexname)) {
    _schema[SCHEMA_INDEX][indexname] = IndexType::Uninitialize;
  }
}

bool GStorageEngine::isMapExist(const std::string& prop) {
  if (_schema.empty()) return false;
  const auto& props = _schema[SCHEMA_CLASS];
  if (props.is_null()) return false;
  return props.count(prop) != 0;
}

bool GStorageEngine::isIndexExist(const std::string& name)
{
  if (_schema.empty()) return false;
  const auto& indexes = _schema[SCHEMA_INDEX];
  if (indexes.is_null()) return false;
  return indexes.count(name) != 0;
}

IndexType GStorageEngine::updateIndexType(const std::string& name, IndexType type)
{
  assert(isIndexExist(name));
  if (_schema[SCHEMA_INDEX][name] == IndexType::Uninitialize) {
    _schema[SCHEMA_INDEX][name] = type;
  }
  return _schema[SCHEMA_INDEX][name];
}

IndexType GStorageEngine::getIndexType(const std::string& name)
{
  assert(isIndexExist(name));
  return _schema[SCHEMA_INDEX][name];
}

std::list<std::tuple<std::string, std::string, std::string>> GStorageEngine::getRelations(const std::string& prop) {
  std::list<std::tuple<std::string, std::string, std::string>> relations;
  auto& edges = _schema[SCHEMA_EDGE];
  for (auto& item : edges.items()) {
    std::string from, to;
    std::tie(from, to) = std::pair<std::string, std::string>(item.value());
    if (from == prop || to == prop) {
      relations.emplace_back(make_tuple(item.key(), from, to));
    }
  }
  return relations;
}

void GStorageEngine::tryInitKeyType(const std::string& prop, KeyType type)
{
  if (_schema[SCHEMA_CLASS][prop][SCHEMA_CLASS_KEY] != KeyType::Uninitialize) return;
  _schema[SCHEMA_CLASS][prop][SCHEMA_CLASS_KEY] = type;
}

void GStorageEngine::tryInitAttributeType(nlohmann::json& attributes, const std::string& attr, const nlohmann::json& value)
{
  if (attributes.count(attr) == 0) {
    if (attributes.size() > 255) {
      // throw error?
      throw std::exception();
    }
    uint8_t index = attributes.size();
    switch ((nlohmann::json::value_t)value) {
    case nlohmann::json::value_t::object:
      if (value.count(OBJECT_TYPE_NAME)) {
        attributes[attr] = std::make_pair((AttributeKind)value[OBJECT_TYPE_NAME], index);
      }
      else {
        attributes[attr] = std::make_pair(AttributeKind::String, index);
      }
      break;
    case nlohmann::json::value_t::array:
      //attributes[attr] = std::pair(AttributeKind::A, index);
      //break;
    case nlohmann::json::value_t::string:
      attributes[attr] = std::make_pair(AttributeKind::String, index);
      break;
    case nlohmann::json::value_t::number_integer:
    case nlohmann::json::value_t::number_unsigned:
      attributes[attr] = std::make_pair(AttributeKind::Integer, index);
      break;
    case nlohmann::json::value_t::number_float:
      attributes[attr] = std::make_pair(AttributeKind::Number, index);
      break;
    case nlohmann::json::value_t::binary:
      attributes[attr] = std::make_pair(AttributeKind::Binary, index);
      break;
    default:
      break;
    }
  }
}

void GStorageEngine::appendValue(uint8_t attrIndex, AttributeKind kind, const nlohmann::json& value, std::string& data)
{
  data.push_back(attrIndex);
  switch (kind)
  {
  case AttributeKind::String:
    data.append(value.dump());
    break;
  case AttributeKind::Binary:
    break;
  case AttributeKind::Number:
  {
    double v = value;
    char buf[sizeof(double)] = { 0 };
    std::memcpy(buf, &v, sizeof(double));
    data.append(buf, sizeof(double));
  }
  break;
  case AttributeKind::Datetime:
    break;
  default:
    break;
  }
}

nlohmann::json GStorageEngine::getProp(const std::string& prop)
{
  const auto& props = _schema[SCHEMA_CLASS];
  return props[prop];
}

mdbx::map_handle GStorageEngine::getOrCreateHandle(const std::string& prop, mdbx::key_mode mode) {
  thread_local auto id = std::this_thread::get_id();
  if (_mHandles[id].count(prop) == 0) {
    mdbx::map_handle propMap;
    GRAPH_EXCEPTION_CATCH(propMap = _txns[id].open_map(prop, (mdbx::key_mode)MDBX_db_flags_t::MDBX_DB_ACCEDE, mdbx::value_mode::single));
    if (!propMap) {
      GRAPH_EXCEPTION_CATCH(propMap = _txns[id].create_map(prop, mode, mdbx::value_mode::single));
    }
    _mHandles[id][prop] = propMap;
  }
  return _mHandles[id][prop];
}

int GStorageEngine::write(const std::string& prop, const std::string& key, void* value, size_t len) {
  if (isMapExist(prop)) {
    tryInitKeyType(prop, KeyType::Byte);
  }
  else if (isIndexExist(prop)) {
    updateIndexType(prop, IndexType::Word);
  }
  auto handle = getOrCreateHandle(prop, mdbx::key_mode::usual);
  thread_local auto id = std::this_thread::get_id();

  size_t cSize = len;
  void* buffer = value;
  mdbx::slice data(buffer, cSize);
  if (0 == ::put(_txns[id], handle, key, data)) {
    return ECode_Success;
  }
  return ECode_Fail;
}

int GStorageEngine::write(const std::string& mapname, const std::string& key, const nlohmann::json& value)
{
  if (isMapExist(mapname)) {
    tryInitKeyType(mapname, KeyType::Byte);
  }
  else if (isIndexExist(mapname)) {
    updateIndexType(mapname, IndexType::Word);
  }
  auto& attributes = _schema[SCHEMA_CLASS][mapname][SCHEMA_CLASS_VALUE];
  // TODO: convert json to gqlite store format: attribute index(max value is 256), [if binary, here put size]data, index, data, ...
  nlohmann::json store;
  for (auto itr = value.begin(), end = value.end(); itr!=end;++itr)
  {
    const std::string& attr = itr.key();
    auto& value = itr.value();
    tryInitAttributeType(attributes, attr, value);
    //std::pair<AttributeKind, uint8_t> info = attributes[attr];
    //appendValue(info.second, info.first, value, data);
  }
  std::string data = value.dump();
  return write(mapname, key, (void*)data.data(), data.size());
}

int GStorageEngine::write(const std::string& mapname, uint64_t key, const nlohmann::json& value)
{
  if (isMapExist(mapname)) {
    tryInitKeyType(mapname, KeyType::Integer);
  }
  else if (isIndexExist(mapname)) {
    updateIndexType(mapname, IndexType::Number);
  }
  auto& attributes = _schema[SCHEMA_CLASS][mapname][SCHEMA_CLASS_VALUE];
  for (auto itr = value.begin(), end = value.end(); itr != end; ++itr) {
    const std::string& attr = itr.key();
    auto& value = itr.value();
    std::string sv = value.dump();
    tryInitAttributeType(attributes, attr, value);
    //std::pair<AttributeKind, uint8_t> info = attributes[attr];
    //appendValue(info.second, info.first, value, data);
  }
  std::string data = value.dump();
  write(mapname, key, (void*)data.data(), data.size());
  return 0;
}

int GStorageEngine::del(const std::string& mapname, uint64_t key, bool from)
{
  auto is_match_from = [](bool from, const gql::edge_id& edge, uint64_t key) {
    return edge._from_len == sizeof(uint64_t) && *(uint64_t*)edge._value == key;
  };
  auto is_match_to = [](bool to, const gql::edge_id& edge, uint64_t key) {
    return (edge._len - edge._from_len) == sizeof(uint64_t) && *(uint64_t*)(edge._value + edge._from_len) == key;
  };

  thread_local auto id = std::this_thread::get_id();
  auto handle = getOrCreateHandle(mapname, mdbx::key_mode::ordinal);
  auto cursor = _txns[id].open_cursor(handle);
  auto data = cursor.to_first(false);
  while (data) {
    std::string k((char*)data.key.byte_ptr(), data.key.size());
    auto edge = gql::to_edge_id(k);
    if (is_match_from(from, edge, key) || is_match_to(!from, edge, key)) {
        _txns[id].erase(handle, data.key);
    }
    gql::release_edge_id(edge);
    data = cursor.to_next(false);
  }
  return 0;
}

int GStorageEngine::del(const std::string& mapname, const std::string& key, bool from) {
  auto is_match_from = [](bool from, const gql::edge_id& edge, const std::string& key) {
    return from && edge._from_len == key.size() && key == edge._value;
  };
  auto is_match_to = [](bool to, const gql::edge_id& edge, const std::string& key) {
    return to && (edge._len - edge._from_len) == key.size() && key == (edge._value + edge._from_len);
  };

  thread_local auto id = std::this_thread::get_id();
  auto handle = getOrCreateHandle(mapname, mdbx::key_mode::ordinal);
  auto cursor = _txns[id].open_cursor(handle);
  auto data = cursor.to_first(false);
  while (data) {
    std::string k((char*)data.key.byte_ptr(), data.key.size());
    auto edge = gql::to_edge_id(k);
    if (is_match_from(from, edge, key) || is_match_to(!from, edge, key)) {
      _txns[id].erase(handle, data.key);
    }
    gql::release_edge_id(edge);
    data = cursor.to_next(false);
  }
  return 0;
}

int GStorageEngine::read(const std::string& prop, const std::string& key, std::string& value) {
  assert(isMapExist(prop) || isIndexExist(prop));
  auto handle = getOrCreateHandle(prop, mdbx::key_mode::usual);
  thread_local auto id = std::this_thread::get_id();
  mdbx::slice data = ::get(_txns[id], handle, key);
  if (data.empty()) return ECode_DATUM_Not_Exist;
  value.assign((char*)data.data(), data.size());
  return ECode_Success;
}

int GStorageEngine::read(const std::string& mapname, uint64_t from, uint64_t to, std::list<std::string>& value)
{
  thread_local auto id = std::this_thread::get_id();
  auto handle = getOrCreateHandle(mapname, mdbx::key_mode::ordinal);
  auto cursor = _txns[id].open_cursor(handle);
  mdbx::slice t(&to, sizeof(uint64_t));
  auto result = cursor.move(mdbx::cursor::key_lowerbound, t);
  while (result && *(uint64_t*)result.key.byte_ptr() > from)
  {
    value.push_back({ (char*)result.value.byte_ptr(), result.value.size() });
  }
  return ECode_Success;
}

int GStorageEngine::parse(const std::string& data, nlohmann::json& value)
{
  return ECode_Success;
}

size_t GStorageEngine::estimate(const std::string& mapname)
{
  if (isIndexExist(mapname)) {
    auto first = getIndexCursor(mapname);
    first.to_first(false);
    if (first.on_last()) return 0;
    auto last = getIndexCursor(mapname);
    last.to_last(false);
    return mdbx::estimate(first, last);
  }
  if (isMapExist(mapname)) {
    auto first = getMapCursor(mapname);
    first.to_first(false);
    if (first.on_last()) return 0;
    auto last = getMapCursor(mapname);
    last.to_last(false);
    return mdbx::estimate(first, last);
  }
  return std::numeric_limits<size_t>::max();
}

int GStorageEngine::del(const std::string& mapname, const std::string& key)
{
  assert(isMapExist(mapname) || isIndexExist(mapname));
  auto handle = getOrCreateHandle(mapname, mdbx::key_mode::usual);
  thread_local auto id = std::this_thread::get_id();
  if (::del(_txns[id], handle, key)) return ECode_Fail;
  return ECode_Success;
}

int GStorageEngine::del(const std::string& mapname, uint64_t key)
{
  assert(isMapExist(mapname) || isIndexExist(mapname));
  auto handle = getOrCreateHandle(mapname, mdbx::key_mode::ordinal);
  thread_local auto id = std::this_thread::get_id();
  if (::del(_txns[id], handle, key)) return ECode_Fail;
  return ECode_Success;
}

int GStorageEngine::write(const std::string& prop, uint64_t key, void* value, size_t len) {
  if (isMapExist(prop)) {
    tryInitKeyType(prop, KeyType::Integer);
  }
  else if (isIndexExist(prop)) {
    updateIndexType(prop, IndexType::Number);
  }
  auto handle = getOrCreateHandle(prop, mdbx::key_mode::ordinal);
  // compress
  void* buffer = value;
  size_t cSize = len;
  thread_local auto id = std::this_thread::get_id();
  mdbx::slice data(buffer, cSize);
  if (0 == ::put(_txns[id], handle, key, data)) {
    return ECode_Success;
  }
  return ECode_Fail;
}
int GStorageEngine::read(const std::string& prop, uint64_t key, std::string& value) {
  assert(isMapExist(prop) || isIndexExist(prop));
  auto handle = getOrCreateHandle(prop, mdbx::key_mode::ordinal);
  thread_local auto id = std::this_thread::get_id();
  mdbx::slice data = ::get(_txns[id], handle, key);
  if (data.empty()) return ECode_DATUM_Not_Exist;
  assert(data.size() != std::numeric_limits<size_t>::max());
  value.assign((char*)data.data(), data.size());
  return ECode_Success;
}

GStorageEngine::cursor GStorageEngine::getMapCursor(const std::string& prop)
{
  assert(isMapExist(prop));
  mdbx::map_handle handle;
  auto pp = getProp(prop);
  auto type = (KeyType)pp[SCHEMA_CLASS_KEY];
  if (type == KeyType::Integer) {
    handle = getOrCreateHandle(prop, mdbx::key_mode::ordinal);
  }
  else {
    handle = getOrCreateHandle(prop, mdbx::key_mode::usual);
  }
  thread_local auto id = std::this_thread::get_id();
  return _txns[id].open_cursor(handle);
}

GStorageEngine::cursor GStorageEngine::getIndexCursor(const std::string& mapname)
{
  assert(isIndexExist(mapname));
  mdbx::map_handle handle;
  switch (getIndexType(mapname)) {
  case IndexType::Number:
    handle = getOrCreateHandle(mapname, mdbx::key_mode::ordinal);
    break;
  case IndexType::Word:
    handle = getOrCreateHandle(mapname, mdbx::key_mode::usual);
    break;
  case IndexType::Vector: // Support number's element only
  default:
    handle = getOrCreateHandle(mapname, mdbx::key_mode::ordinal);
    break;
  }
  thread_local auto id = std::this_thread::get_id();
  return _txns[id].open_cursor(handle);
}

int GStorageEngine::startTrans(ReadWriteOption opt) {
  thread_local auto id = std::this_thread::get_id();
  mdbx::txn_managed txn;
  if (_txns.count(id) == 0) {
    if (opt == ReadWriteOption::read_only) {
      txn = _env.start_read();
    }
    else {
      txn = _env.start_write();
    }
    if (!txn) return ECODE_NULL_PTR;
    _txns[id] = std::move(txn);
  }
  return ECode_Success;
}

// int GStorageEngine::finishTrans() {
//   thread_local auto id = std::this_thread::get_id();
//   if (_txns.count(id) == 0) return ECode_TRANSTION_Not_Exist;
//   _txns[id].commit();
//   return ECode_Success;
// }

KeyType GStorageEngine::getKeyType(const std::string& m) const
{
  if (_schema[SCHEMA_CLASS][m].empty()) return KeyType::Uninitialize;
  return _schema[SCHEMA_CLASS][m][SCHEMA_CLASS_KEY];
}

std::vector<std::string> GStorageEngine::getIndexes() const
{
  std::vector<std::string> v;
  if (_schema.empty() || _schema.count(SCHEMA_INDEX) == 0 || _schema[SCHEMA_INDEX].empty()) return v;
  for (auto itr = _schema[SCHEMA_INDEX].begin(), end = _schema[SCHEMA_INDEX].end(); itr != end;++itr) {
    v.emplace_back(itr.key());
  }
  return v;
}

void GStorageEngine::upsetNode(node_t id, GEntityNode* node) {
  _nodes[id] = node;
}

GEntityNode* GStorageEngine::getNode(node_t id) {
  return _nodes[id];
}

int upsetVertex(GStorageEngine* storage, GEntityNode* entityNode) {
  std::string groupName = storage->getGroupName(entityNode->gid());
  int ret = storage->write(groupName, entityNode->id(), entityNode->attributes());
  if (ret == ECode_Success)
    storage->upsetNode(entityNode->id(), entityNode);
  return ret;
}

int upsetEdge(GStorageEngine* storage, GEntityEdge* entityEdge) {
  // https://betterprogramming.pub/native-graph-database-storage-7ed8ebabe6d8
  auto&& eid = entityEdge->id();
  auto&& edgeGroup = storage->getGroupName(entityEdge->gid());
  Variant<std::string, uint64_t> src, dst;
  gql::get_from_to(eid, src, dst);

  auto lambdaSetNodeMap = [&src, storage] (GEntityNode* node, const edge2_t& edgeID) {
    group_t gid = node->gid();
    std::string groupName = storage->getGroupName(gid);
    if (!node->isUpdate()) {
      src.visit([&groupName, storage, &edgeID](std::string key) {
          // new node
          storage->write(groupName, key, (void*)edgeID.data(), edgeID.size());
      },
      [&groupName, storage, &edgeID](uint64_t key) {
          // new node
          storage->write(groupName, key, (void*)edgeID.data(), edgeID.size());
      });
      node->setUpdate(true);
    }
  };

  auto srcNode = entityEdge->from();
  auto dstNode = entityEdge->to();
  if (EdgeChangedStatus::Latest == entityEdge->status()) {
    lambdaSetNodeMap(srcNode, eid);
    lambdaSetNodeMap(dstNode, eid);
    
    // update node's edge list
    std::string src_prev_rid = srcNode->prev(eid);
    std::string src_next_rid = srcNode->next(eid);
    std::string dst_prev_rid = dstNode->prev(eid);
    std::string dst_next_rid = dstNode->next(eid);
    std::string data = src_prev_rid + "," + src_next_rid + "," + dst_prev_rid + "," + dst_next_rid;
    storage->write(edgeGroup, eid, data.data(), data.size());
    entityEdge->setUpdate(true);
  }
  
  // update connect node
  auto lambdaUpdateEdge = [storage, &edgeGroup] (const edge2_t& eid, GEntityNode* node) {
    auto nodeID = node->id();
    auto& edgesID = node->edges();
    for (auto& edgeID: edgesID) {
      if (edgeID == eid)
        continue;

      std::string data;
      storage->read(edgeGroup, edgeID, data);
      auto&& rids = gql::split(data, ',');
      assert(rids.size() == 4);
      Variant<std::string, uint64_t> nodes[2];
      gql::get_from_to(edgeID, nodes[0], nodes[1]);
      auto node = storage->getNode(nodeID);
      std::string prev_rid = node->prev(edgeID);
      std::string next_rid = node->next(edgeID);
      if (nodes[0].Get<node_t>() == nodeID) {
        rids[0] = prev_rid;
        rids[1] = next_rid;
        data = rids[0] + "," + rids[1] + "," + rids[2] + "," + rids[3];
        storage->write(edgeGroup, edgeID, data.data(), data.size());
      }
      else if (nodes[1].Get<node_t>() == nodeID) {
        rids[2] = prev_rid;
        rids[3] = next_rid;
        data = rids[0] + "," + rids[1] + "," + rids[2] + "," + rids[3];
        storage->write(edgeGroup, edgeID, data.data(), data.size());
      }
    }
  };

  // update connect edge
  lambdaUpdateEdge(eid, srcNode);
  lambdaUpdateEdge(eid, dstNode);
  // update properties

  return ECode_Success;
}

std::list<node_t> getVertexNeighbors(GStorageEngine* storage, group_t gid, node_t nid) {
  std::list<node_t> neighbors;
  return neighbors;
}

std::list<edge2_t> getVertexOutbound(GStorageEngine* storage, group_t edgeGroup, group_t nodeGroup, node_t nid) {
  std::list<edge2_t> outbound;

  auto&& edgeGroupName = storage->getGroupName(edgeGroup);
  auto&& nodeGroupName = storage->getGroupName(nodeGroup);
  std::string edgeID, startID;
  storage->read(nodeGroupName, nid, edgeID);
  startID = edgeID;
  while (!edgeID.empty()) {
    Variant<std::string, node_t> src, dst;
    gql::get_from_to(edgeID, src, dst);
    if (dst.Get<node_t>() == nid) {
      outbound.push_back(edgeID);
    }
    std::string data;
    storage->read(edgeGroupName, edgeID, data);
    auto&& edges = gql::split(data, ',');
    if (edges.size() == 0 || edges[3] == startID)
      break;
      
    edgeID = edges[3];
  }
  return outbound;
}
