#include <arpa/inet.h>
#include <iostream>
#include <memory>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <variant>
#include <vector>

// 导入我们强大的 resp 解析/序列化模块
import resp;
// 导入新的通用客户端工具模块
import client_utils;

void print_resp_response(std::string &response_buffer) {
  std::string_view buffer_view(response_buffer);
  auto result = resp::parse(buffer_view);

  if (result) {
    common::ClientUtils::print_resp_value(*result);
    // 从缓冲区移除已解析的部分
    response_buffer.erase(0, response_buffer.length() - buffer_view.length());
  } else {
    if (result.error() == resp::ParseError::Incomplete) {
      std::cout << "(incomplete response, waiting for more data...)"
                << std::endl;
    } else {
      std::cout << "Error parsing response: " << response_buffer << std::endl;
    }
  }
}

int main(int argc, char *argv[]) {
  // 默认连接到 localhost:6379
  const char *host = "127.0.0.1";
  int port = 6379;

  // 创建 socket
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    std::cerr << "Failed to create socket" << std::endl;
    return 1;
  }

  // 设置服务器地址
  sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);

  // 转换 IP 地址
  if (inet_pton(AF_INET, host, &server_addr.sin_addr) <= 0) {
    std::cerr << "Invalid address / Address not supported" << std::endl;
    return 1;
  }

  // 连接到服务器
  if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
    std::cerr << "Connection failed" << std::endl;
    return 1;
  }

  std::cout << "Connected to " << host << ":" << port << std::endl;

  // 交互式命令循环
  std::string line;
  std::string response_buffer;
  char read_buffer[4096];

  while (true) {
    std::cout << "> ";
    if (!std::getline(std::cin, line)) {
      break; // End of input
    }

    if (line.empty()) {
      continue;
    }

    if (line == "quit" || line == "exit") {
      break;
    }

    // 使用通用工具序列化命令
    std::string request = common::ClientUtils::serialize_command(line);

    // 发送命令
    if (send(sock, request.c_str(), request.length(), 0) < 0) {
      std::cerr << "Send failed" << std::endl;
      break;
    }

    // 持续读取和解析，直到解析出一个完整的响应
    while (true) {
      std::string_view buffer_view(response_buffer);
      auto parse_result = resp::parse(buffer_view);
      if (parse_result) {
        common::ClientUtils::print_resp_value(*parse_result);
        response_buffer.erase(0, response_buffer.size() - buffer_view.size());
        break; // 成功解析一个，跳出读循环
      } else if (parse_result.error() != resp::ParseError::Incomplete) {
        std::cout << "(error) Malformed response from server." << std::endl;
        response_buffer.clear(); // 清空错误数据
        break;
      }

      // 如果数据不完整，继续读取
      ssize_t bytes_read = read(sock, read_buffer, sizeof(read_buffer) - 1);
      if (bytes_read > 0) {
        response_buffer.append(read_buffer, bytes_read);
      } else {
        // 读取错误或连接关闭
        if (bytes_read < 0)
          perror("read");
        std::cout << "Connection closed by server." << std::endl;
        close(sock);
        return 1;
      }
    }
  }

  close(sock);
  return 0;
}