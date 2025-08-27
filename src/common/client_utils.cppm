// src/common/client_utils.cppm
module;

#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

export module client_utils;

import resp;

export namespace common {

class ClientUtils {
public:
  // 将命令字符串（例如 "SET key value"）转换为 RESP 数组格式
  static std::string serialize_command(const std::string &command_line) {
    std::stringstream ss(command_line);
    std::string token;
    std::vector<std::string> parts;
    while (ss >> token) {
      parts.push_back(token);
    }

    std::string resp_command = "*" + std::to_string(parts.size()) + "\r\n";
    for (const auto &part : parts) {
      resp_command += "$" + std::to_string(part.length()) + "\r\n";
      resp_command += part + "\r\n";
    }
    return resp_command;
  }

  // 使用 std::visit 来优雅地打印不同类型的 RESP 响应
  static void print_resp_value(const resp::RespValue &value) {
    std::visit(
        [](const auto &val) {
          using T = std::decay_t<decltype(val)>;
          if constexpr (std::is_same_v<T, resp::RespSimpleString>) {
            std::cout << val.value << std::endl;
          } else if constexpr (std::is_same_v<T, resp::RespError>) {
            std::cout << "(error) " << val.value << std::endl;
          } else if constexpr (std::is_same_v<T, resp::RespInteger>) {
            std::cout << "(integer) " << val.value << std::endl;
          } else if constexpr (std::is_same_v<T, resp::RespBulkString>) {
            if (val.value) {
              std::cout << "\"" << *val.value << "\"" << std::endl;
            } else {
              std::cout << "(nil)" << std::endl;
            }
          } else if constexpr (std::is_same_v<T, resp::RespNull>) {
            std::cout << "(nil)" << std::endl;
          } else if constexpr (std::is_same_v<
                                   T, std::unique_ptr<resp::RespArray>>) {
            std::cout << "Array (" << val->values.size()
                      << " elements):" << std::endl;
            for (size_t i = 0; i < val->values.size(); ++i) {
              std::cout << i + 1 << ") ";
              print_resp_value(val->values[i]);
            }
          }
        },
        value);
  }
};

} // namespace common