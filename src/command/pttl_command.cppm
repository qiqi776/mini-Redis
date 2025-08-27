module;

#include <chrono>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

export module pttl_command;

import command_defs;
import resp;
import logger;

// PTTL命令
export class PTTLCommand : public Command {
public:
  PTTLCommand(std::span<const resp::RespValue> args,
              const resp::RespValue &original_command, KVServerContext &context)
      : args_(args), original_command_(original_command), context_(context) {}

  std::string execute() override {
    if (args_.size() != 1) {
      LOG_WARN("PTTL命令参数数量错误: {}", args_.size());
      return resp::serialize_error(
          "ERR wrong number of arguments for 'PTTL' command");
    }

    // 解析键名
    const auto *key_variant = std::get_if<resp::RespBulkString>(&args_[0]);
    if (!key_variant || !key_variant->value.has_value()) {
      LOG_WARN("PTTL命令的键参数无效");
      return resp::serialize_error("ERR key must be a non-null bulk string");
    }
    const std::string &key = *key_variant->value;

    // 查找键
    auto &db = context_.get_db();
    auto it = db.find(key);
    if (it == db.end()) {
      LOG_DEBUG("PTTL命令的键不存在: {}", key);
      return resp::serialize_integer(-2); // 键不存在返回-2
    }

    // 检查键是否有过期时间
    if (!it->second.expires_at.has_value()) {
      LOG_DEBUG("PTTL命令的键没有设置过期时间: {}", key);
      return resp::serialize_integer(-1); // 键存在但没有过期时间返回-1
    }

    // 计算剩余时间（毫秒）
    auto now = std::chrono::steady_clock::now();
    auto pttl = std::chrono::duration_cast<std::chrono::milliseconds>(
                    it->second.expires_at.value() - now)
                    .count();

    // 如果键已过期，先删除它
    if (pttl <= 0) {
      LOG_DEBUG("PTTL命令发现过期键: {}", key);
      db.erase(it);
      return resp::serialize_integer(-2); // 已经过期的键视为不存在
    }

    LOG_DEBUG("键 {} 的剩余生存时间: {} 毫秒", key, pttl);
    return resp::serialize_integer(pttl);
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