module;

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

export module set_command;

import command_defs;
import resp;
import logger;

// Set命令
export class SetCommand : public Command {
public:
  SetCommand(std::span<const resp::RespValue> args,
             const resp::RespValue &original_command, KVServerContext &context,
             bool from_aof)
      : args_(args), original_command_(original_command), context_(context),
        from_aof_(from_aof) {}

  std::string execute() override {
    if (args_.size() != 2) {
      LOG_WARN("SET命令参数数量错误: {}", args_.size());
      return resp::serialize_error(
          "ERR wrong number of arguments for 'SET' command");
    }

    const auto *key_variant = std::get_if<resp::RespBulkString>(&args_[0]);
    const auto *value_variant = std::get_if<resp::RespBulkString>(&args_[1]);

    if (!key_variant || !key_variant->value.has_value() || !value_variant ||
        !value_variant->value.has_value()) {
      LOG_WARN("SET命令的键或值参数无效");
      return resp::serialize_error(
          "ERR key and value must be non-null bulk strings");
    }

    const std::string &key = *key_variant->value;
    const std::string &value = *value_variant->value;
    auto &db = context_.get_db();
    bool is_new = db.find(key) == db.end();

    // 保存值并清除任何过期时间
    db[key] = KeyValue{value, std::nullopt};

    if (is_new) {
      LOG_DEBUG("SET命令创建新键: {}", key);
    } else {
      LOG_DEBUG("SET命令更新键: {}", key);
    }

    return resp::serialize_ok();
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