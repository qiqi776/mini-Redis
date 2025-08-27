module;

#include <charconv>
#include <chrono>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

export module expire_command;

import command_defs;
import resp;
import logger;

// Expire命令
export class ExpireCommand : public Command {
public:
  ExpireCommand(std::span<const resp::RespValue> args,
                const resp::RespValue &original_command,
                KVServerContext &context, bool from_aof)
      : args_(args), original_command_(original_command), context_(context),
        from_aof_(from_aof) {}

  std::string execute() override {
    if (args_.size() != 2) {
      LOG_WARN("EXPIRE命令参数数量错误: {}", args_.size());
      return resp::serialize_error(
          "ERR wrong number of arguments for 'EXPIRE' command");
    }

    // 解析键名
    const auto *key_variant = std::get_if<resp::RespBulkString>(&args_[0]);
    if (!key_variant || !key_variant->value.has_value()) {
      LOG_WARN("EXPIRE命令的键参数无效");
      return resp::serialize_error("ERR key must be a non-null bulk string");
    }
    const std::string &key = *key_variant->value;

    // 解析过期秒数
    const auto *seconds_variant = std::get_if<resp::RespBulkString>(&args_[1]);
    if (!seconds_variant || !seconds_variant->value.has_value()) {
      LOG_WARN("EXPIRE命令的秒数参数无效");
      return resp::serialize_error(
          "ERR seconds must be a non-null bulk string");
    }

    // 转换秒数为整数
    int seconds;
    const auto &seconds_str = *seconds_variant->value;
    auto result = std::from_chars(
        seconds_str.data(), seconds_str.data() + seconds_str.size(), seconds);

    if (result.ec != std::errc() ||
        result.ptr != seconds_str.data() + seconds_str.size()) {
      LOG_WARN("EXPIRE命令的秒数参数不是有效整数: {}", *seconds_variant->value);
      return resp::serialize_error(
          "ERR value is not an integer or out of range");
    }

    // 秒数必须为正数
    if (seconds < 0) {
      LOG_WARN("EXPIRE命令的秒数参数必须为正数: {}", seconds);
      return resp::serialize_error("ERR seconds must be positive");
    }

    // 查找键
    auto &db = context_.get_db();
    auto it = db.find(key);
    if (it == db.end()) {
      LOG_DEBUG("EXPIRE命令的键不存在: {}", key);
      return resp::serialize_integer(0); // 键不存在返回0
    }

    // 设置过期时间
    auto now = std::chrono::steady_clock::now();
    auto expire_time = now + std::chrono::seconds(seconds);
    it->second.expires_at = expire_time;

    LOG_DEBUG("设置键 {} 在 {} 秒后过期", key, seconds);
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