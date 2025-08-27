module;

#include <format>
#include <string>

export module unknown_command;

import command_defs;
import resp;
import logger;

// 未知命令处理
export class UnknownCommand : public Command {
public:
  UnknownCommand(const std::string &name,
                 const resp::RespValue &original_command)
      : name_(name), original_command_(original_command) {}

  std::string execute() override {
    LOG_WARN("未知命令: '{}'", name_);
    return resp::serialize_error(
        std::format("ERR unknown command '{}'", name_));
  }

  bool should_replicate() const override { return false; }
  const resp::RespValue &get_original_command() const override {
    return original_command_;
  }

private:
  std::string name_;
  const resp::RespValue &original_command_;
};