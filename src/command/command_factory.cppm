module;

#include <cctype>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

export module command_factory;

import command_defs;
import get_command;
import set_command;
import expire_command;
import pexpire_command;
import ttl_command;
import pttl_command;
import persist_command;
import info_command;
import unknown_command;
import resp;
import logger;

// 命令工厂实现
export class KVCommandFactory : public CommandFactory {
public:
  KVCommandFactory(KVServerContext &context) : context_(context) {
    command_map_["GET"] = [](auto args, auto &cmd, auto &ctx, auto) {
      return std::make_unique<GetCommand>(args, cmd, ctx);
    };
    command_map_["SET"] = [](auto args, auto &cmd, auto &ctx, auto from_aof) {
      return std::make_unique<SetCommand>(args, cmd, ctx, from_aof);
    };
    command_map_["EXPIRE"] = [](auto args, auto &cmd, auto &ctx,
                                auto from_aof) {
      return std::make_unique<ExpireCommand>(args, cmd, ctx, from_aof);
    };
    command_map_["PEXPIRE"] = [](auto args, auto &cmd, auto &ctx,
                                 auto from_aof) {
      return std::make_unique<PExpireCommand>(args, cmd, ctx, from_aof);
    };
    command_map_["TTL"] = [](auto args, auto &cmd, auto &ctx, auto) {
      return std::make_unique<TTLCommand>(args, cmd, ctx);
    };
    command_map_["PTTL"] = [](auto args, auto &cmd, auto &ctx, auto) {
      return std::make_unique<PTTLCommand>(args, cmd, ctx);
    };
    command_map_["PERSIST"] = [](auto args, auto &cmd, auto &ctx,
                                 auto from_aof) {
      return std::make_unique<PersistCommand>(args, cmd, ctx, from_aof);
    };
    command_map_["INFO"] = [](auto, auto &cmd, auto &ctx, auto) {
      return std::make_unique<InfoCommand>(cmd, ctx);
    };
  }

  std::unique_ptr<Command>
  create_command(const resp::RespValue &command_variant,
                 bool from_aof) override {

    if (!std::holds_alternative<std::unique_ptr<resp::RespArray>>(
            command_variant)) {
      LOG_WARN("无效的命令格式: 不是数组类型");
      // 创建一个特殊的错误命令
      return std::make_unique<UnknownCommand>("invalid_format",
                                              command_variant);
    }

    const auto &arr =
        std::get<std::unique_ptr<resp::RespArray>>(command_variant)->values;

    if (arr.empty()) {
      LOG_WARN("收到空命令");
      return std::make_unique<UnknownCommand>("empty_command", command_variant);
    }

    const auto *command_name_val = std::get_if<resp::RespBulkString>(&arr[0]);
    if (!command_name_val || !command_name_val->value.has_value()) {
      LOG_WARN("命令名不是有效的字符串");
      return std::make_unique<UnknownCommand>("invalid_command_name",
                                              command_variant);
    }

    // 安全地创建 string_view
    std::string_view command_name_sv = *command_name_val->value;

    std::string command_upper;
    command_upper.reserve(command_name_sv.length());
    for (char c : command_name_sv) {
      command_upper += std::toupper(c);
    }

    // 使用 std::span 创建参数视图，避免不必要的拷贝
    std::span<const resp::RespValue> args = arr;
    args = args.subspan(1); // 移除命令名，只保留参数

    // 根据命令名创建对应的命令对象
    if (auto it = command_map_.find(command_upper); it != command_map_.end()) {
      return it->second(args, command_variant, context_, from_aof);
    } else {
      return std::make_unique<UnknownCommand>(std::string(command_name_sv),
                                              command_variant);
    }
  }

private:
  KVServerContext &context_;
  using CommandCreator = std::function<std::unique_ptr<Command>(
      std::span<const resp::RespValue>, const resp::RespValue &,
      KVServerContext &, bool)>;
  std::unordered_map<std::string, CommandCreator> command_map_;
};