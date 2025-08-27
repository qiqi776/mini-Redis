module;

#include <string>

export module info_command;

import command_defs;
import resp;

// INFO命令
export class InfoCommand : public Command {
public:
  InfoCommand(const resp::RespValue &original_command, KVServerContext &context)
      : original_command_(original_command), context_(context) {}

  std::string execute() override {
    auto &stats = context_.get_stats();
    auto &db = context_.get_db();
    return resp::serialize_bulk_string(stats.get_info(db.size()));
  }

  bool should_replicate() const override { return false; }
  const resp::RespValue &get_original_command() const override {
    return original_command_;
  }

private:
  const resp::RespValue &original_command_;
  KVServerContext &context_;
};