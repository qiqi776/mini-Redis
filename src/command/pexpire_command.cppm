module;

#include <charconv>
#include <chrono>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

export module pexpire_command;

import command_defs;
import resp;
import logger;

// PExpire命令
export class PExpireCommand : public Command {
public:
  PExpireCommand(std::span<const resp::RespValue> args,
                 const resp::RespValue &original_command,
                 KVServerContext &context, bool from_aof)
      : args_(args), original_command_(original_command), context_(context),
        from_aof_(from_aof) {}

  std::string execute() override {
    if (args_.size() != 2) {
      LOG_WARN("PEXPIRE命令参数数量错误: {}", args_.size());
      return resp::serialize_error(
          "ERR wrong number of arguments for 'PEXPIRE' command");
    }

    // 解析键名
    const auto *key_variant = std::get_if<resp::RespBulkString>(&args_[0]);
    if (!key_variant || !key_variant->value.has_value()) {
      LOG_WARN("PEXPIRE命令的键参数无效");
      return resp::serialize_error("ERR key must be a non-null bulk string");
    }
    const std::string &key = *key_variant->value;

    // 解析过期毫秒数
    const auto *milliseconds_variant =
        std::get_if<resp::RespBulkString>(&args_[1]);
    if (!milliseconds_variant || !milliseconds_variant->value.has_value()) {
      LOG_WARN("PEXPIRE命令的毫秒数参数无效");
      return resp::serialize_error(
          "ERR milliseconds must be a non-null bulk string");
    }

    // 转换毫秒数为整数
    int64_t milliseconds;
    const auto &milliseconds_str = *milliseconds_variant->value;
    auto result = std::from_chars(
        milliseconds_str.data(),
        milliseconds_str.data() + milliseconds_str.size(), milliseconds);

    if (result.ec != std::errc() ||
        result.ptr != milliseconds_str.data() + milliseconds_str.size()) {
      LOG_WARN("PEXPIRE命令的毫秒数参数不是有效整数: {}",
               *milliseconds_variant->value);
      return resp::serialize_error(
          "ERR value is not an integer or out of range");
    }

    // 毫秒数必须为正数
    if (milliseconds < 0) {
      LOG_WARN("PEXPIRE命令的毫秒数参数必须为正数: {}", milliseconds);
      return resp::serialize_error("ERR milliseconds must be positive");
    }

    // 查找键
    auto &db = context_.get_db();
    auto it = db.find(key);
    if (it == db.end()) {
      LOG_DEBUG("PEXPIRE命令的键不存在: {}", key);
      return resp::serialize_integer(0); // 键不存在返回0
    }

    // 设置过期时间
    auto now = std::chrono::steady_clock::now();
    auto expire_time = now + std::chrono::milliseconds(milliseconds);
    it->second.expires_at = expire_time;

    LOG_DEBUG("设置键 {} 在 {} 毫秒后过期", key, milliseconds);
    return resp::serialize_integer(1); // 成功设置返回1
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