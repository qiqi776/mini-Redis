#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

// 导入新的通用客户端工具模块
import client_utils;

const int PORT = 6379;
const char *IP = "127.0.0.1";
const int BUFFER_SIZE = 1024;

// 运行单个测试用例的辅助函数
// 它会连接服务器，发送命令，接收响应，并与预期结果进行比较
bool run_test_case(const std::string &test_name, const std::string &command,
                   const std::string &expected_response) {
  std::cout << "Running test: " << test_name << "..." << std::endl;

  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock == -1) {
    perror("socket failed");
    return false;
  }

  sockaddr_in serv_addr;
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(PORT);

  if (inet_pton(AF_INET, IP, &serv_addr.sin_addr) <= 0) {
    perror("inet_pton failed");
    close(sock);
    return false;
  }

  if (connect(sock, (sockaddr *)&serv_addr, sizeof(serv_addr)) == -1) {
    perror("connect failed");
    std::cerr
        << "Error: Failed to connect to the server. Is the server running?"
        << std::endl;
    close(sock);
    return false;
  }

  std::string message = common::ClientUtils::serialize_command(command);
  if (write(sock, message.c_str(), message.length()) < 0) {
    perror("write failed");
    close(sock);
    return false;
  }

  char buffer[BUFFER_SIZE] = {0};
  int valread = read(sock, buffer, BUFFER_SIZE - 1);
  close(sock);

  if (valread <= 0) {
    std::cout << "  [FAIL] Did not receive a response from the server."
              << std::endl;
    return false;
  }

  std::string actual_response(buffer, valread);

  // 首先移除所有末尾的 \r\n
  while (!actual_response.empty() &&
         (actual_response.back() == '\n' || actual_response.back() == '\r')) {
    actual_response.pop_back();
  }

  // 将 RESP 响应转换为简单字符串以便比较
  if (!actual_response.empty()) {
    if (actual_response.front() == '+') { // Simple String: +OK
      actual_response = actual_response.substr(1);
    } else if (actual_response.front() == '-') { // Error: -ERR message
      actual_response = actual_response.substr(1);
    } else if (actual_response.front() == '$') { // Bulk String: $5\r\nalice
      size_t first_crlf = actual_response.find("\r\n");
      if (first_crlf != std::string::npos) {
        actual_response = actual_response.substr(first_crlf + 2);
      }
    }
  }

  // RESP 的 (nil) 响应是 "$-1"
  if (std::string(buffer, valread) == "$-1\r\n") {
    actual_response = "(nil)";
  }

  if (actual_response == expected_response) {
    std::cout << "  [PASS] Expected: '" << expected_response << "', Got: '"
              << actual_response << "'" << std::endl;
    return true;
  } else {
    std::cout << "  [FAIL] Expected: '" << expected_response << "', Got: '"
              << actual_response << "'" << std::endl;
    return false;
  }
}

int main() {
  std::cout << "--- Starting Automated K/V Server Test ---" << std::endl;

  std::vector<bool> results;

  results.push_back(run_test_case("设置 name", "SET name alice", "OK"));
  results.push_back(run_test_case("设置 age", "SET age 30", "OK"));
  results.push_back(run_test_case("获取 name", "GET name", "alice"));
  results.push_back(run_test_case("获取 age", "GET age", "30"));
  results.push_back(run_test_case("获取不存在的 key", "GET noname", "(nil)"));

  // --- 错误处理和边界情况测试 ---
  std::cout << "\n--- Testing Error Handling & Edge Cases ---" << std::endl;
  // 大小写不敏感测试
  results.push_back(run_test_case("大小写不敏感 GET", "get name", "alice"));
  results.push_back(run_test_case("大小写不敏感 SET", "sEt name bob", "OK"));
  results.push_back(run_test_case("验证 SET 后大写 GET", "GET name", "bob"));

  // 覆盖旧值
  results.push_back(run_test_case("覆盖 SET", "SET name charlie", "OK"));
  results.push_back(run_test_case("验证覆盖后的值", "GET name", "charlie"));

  // 参数数量错误
  results.push_back(
      run_test_case("GET 参数过多", "GET name extra",
                    "ERR wrong number of arguments for 'GET' command"));
  results.push_back(
      run_test_case("GET 参数过少", "GET",
                    "ERR wrong number of arguments for 'GET' command"));
  results.push_back(
      run_test_case("SET 参数过多", "SET key val extra",
                    "ERR wrong number of arguments for 'SET' command"));
  results.push_back(
      run_test_case("SET 参数过少", "SET key",
                    "ERR wrong number of arguments for 'SET' command"));

  // 未知命令
  results.push_back(run_test_case("未知命令", "UNKNOWN_COMMAND key",
                                  "ERR unknown command 'UNKNOWN_COMMAND'"));

  int failed_count = 0;
  for (bool result : results) {
    if (!result) {
      failed_count++;
    }
  }

  std::cout << "\n--- Test Summary ---" << std::endl;
  if (failed_count == 0) {
    std::cout << "√ All " << results.size() << " tests passed!" << std::endl;
    return 0; // 成功
  } else {
    std::cout << "× " << failed_count << " out of " << results.size()
              << " tests failed." << std::endl;
    return 1; // 失败
  }
}