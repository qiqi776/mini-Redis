module;

#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

export module get_command;

import command_defs;
import resp;
import logger;

// Get命令
export class GetCommand : public Command {
public:
  GetCommand(std::span<const resp::RespValue> args,
             const resp::RespValue &original_command, KVServerContext &context)
      : args_(args), original_command_(original_command), context_(context) {}

  std::string execute() override {
    if (args_.size() != 1) {
      LOG_WARN("GET命令参数数量错误: {}", args_.size());
      return resp::serialize_error(
          "ERR wrong number of arguments for 'GET' command");
    }

    // 使用 std::get_if 安全地获取参数
    const auto *key_variant = std::get_if<resp::RespBulkString>(&args_[0]);
    if (!key_variant || !key_variant->value.has_value()) {
      LOG_WARN("GET命令的键参数无效");
      return resp::serialize_error("ERR key must be a non-null bulk string");
    }
    const std::string &key = *key_variant->value;

    auto &db = context_.get_db();
    auto &stats = context_.get_stats();
    auto it = db.find(key);
    if (it != db.end()) {
      // 惰性删除：如果键已过期，先删除它
      if (context_.is_key_expired(key, it->second)) {
        LOG_DEBUG("GET命令发现过期键: {}", key);
        db.erase(it);
        stats.increment_keyspace_misses();
        return resp::serialize_null_bulk_string();
      }

      LOG_DEBUG("GET命令成功获取键: {}", key);
      stats.increment_keyspace_hits();
      return resp::serialize_bulk_string(it->second.value);
    } else {
      LOG_DEBUG("GET命令键不存在: {}", key);
      stats.increment_keyspace_misses();
      return resp::serialize_null_bulk_string();
    }
  }

  bool should_replicate() const override { return false; }
  const resp::RespValue &get_original_command() const override {
    return original_command_;
  }

private:
  std::span<const resp::RespValue> args_;
  const resp::RespValue &original_command_;
  KVServerContext &context_;
};