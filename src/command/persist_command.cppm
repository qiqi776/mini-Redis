module;

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

export module persist_command;

import command_defs;
import resp;
import logger;

// PERSIST命令
export class PersistCommand : public Command {
public:
  PersistCommand(std::span<const resp::RespValue> args,
                 const resp::RespValue &original_command,
                 KVServerContext &context, bool from_aof)
      : args_(args), original_command_(original_command), context_(context),
        from_aof_(from_aof) {}

  std::string execute() override {
    if (args_.size() != 1) {
      LOG_WARN("PERSIST命令参数数量错误: {}", args_.size());
      return resp::serialize_error(
          "ERR wrong number of arguments for 'PERSIST' command");
    }

    // 解析键名
    const auto *key_variant = std::get_if<resp::RespBulkString>(&args_[0]);
    if (!key_variant || !key_variant->value.has_value()) {
      LOG_WARN("PERSIST命令的键参数无效");
      return resp::serialize_error("ERR key must be a non-null bulk string");
    }
    const std::string &key = *key_variant->value;

    // 查找键
    auto &db = context_.get_db();
    auto it = db.find(key);
    if (it == db.end()) {
      LOG_DEBUG("PERSIST命令的键不存在: {}", key);
      return resp::serialize_integer(0); // 键不存在返回0
    }

    // 检查键是否有过期时间
    if (!it->second.expires_at.has_value()) {
      LOG_DEBUG("PERSIST命令的键没有设置过期时间: {}", key);
      return resp::serialize_integer(0); // 键存在但没有过期时间返回0
    }

    // 移除过期时间
    it->second.expires_at = std::nullopt;
    LOG_DEBUG("移除键 {} 的过期时间", key);
    return resp::serialize_integer(1); // 成功移除过期时间返回1
  }

  bool should_replicate() const override { return !from_aof_; }
  const resp::RespValue &get_original_command() const override {
    return original_command_;
  }

private:
  std::span<const resp::RespValue> args_;
  const resp::RespValue &original_command_;
  KVServerContext &context_;
  bool from_aof_;
};