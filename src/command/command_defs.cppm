module;

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

export module command_defs;

import resp;
import logger;
import aof;
import server_stat;

// 存储结构扩展，包含值和过期时间
export struct KeyValue {
  std::string value;
  // 使用 std::optional 来表示键是否设置了过期时间
  std::optional<std::chrono::time_point<std::chrono::steady_clock>> expires_at;
};

// 定义存储类型别名
export using Storage = std::unordered_map<std::string, KeyValue>;

// 命令接口
export class Command {
public:
  virtual ~Command() = default;
  virtual std::string execute() = 0;
  virtual bool should_replicate() const { return false; }
  virtual const resp::RespValue &get_original_command() const = 0;
};

// 命令工厂接口
export class CommandFactory {
public:
  virtual ~CommandFactory() = default;
  virtual std::unique_ptr<Command>
  create_command(const resp::RespValue &command_variant, bool from_aof) = 0;
};

// KVServer命令上下文 - 提供给命令访问数据库和其他资源的接口
export class KVServerContext {
public:
  KVServerContext(Storage &db, Aof *aof, ServerStat &stats)
      : db_(db), aof_(aof), stats_(stats) {}

  // 数据库操作
  Storage &get_db() { return db_; }
  Aof *get_aof() { return aof_; }
  ServerStat &get_stats() { return stats_; }

  // 键操作辅助函数
  bool is_key_expired(const std::string &key, const KeyValue &kv) {
    if (!kv.expires_at.has_value()) {
      return false; // 没有设置过期时间
    }

    auto now = std::chrono::steady_clock::now();
    return now >= kv.expires_at.value();
  }

  bool delete_expired_key(const std::string &key) {
    auto it = db_.find(key);
    if (it == db_.end()) {
      return false;
    }

    if (!is_key_expired(key, it->second)) {
      return false; // 键未过期
    }

    LOG_DEBUG("删除过期键: {}", key);
    db_.erase(it);
    return true;
  }

private:
  Storage &db_;
  Aof *aof_;
  ServerStat &stats_;
};