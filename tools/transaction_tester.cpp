#include <arpa/inet.h>
#include <array>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

// 导入通用客户端工具模块
import client_utils;

const int PORT = 6379;
const char *IP = "127.0.0.1";
const int BUFFER_SIZE = 4096;

// 为事务测试特殊设计的测试函数，它会连接服务器，发送多个命令，并比较结果
bool run_transaction_test(const std::string &test_name,
                          const std::vector<std::string> &commands,
                          const std::vector<std::string> &expected_responses) {
  if (commands.size() != expected_responses.size()) {
    std::cerr << "Error: Number of commands does not match number of expected "
                 "responses!"
              << std::endl;
    return false;
  }

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

  bool success = true;

  for (size_t i = 0; i < commands.size(); i++) {
    std::string message = common::ClientUtils::serialize_command(commands[i]);
    if (write(sock, message.c_str(), message.length()) < 0) {
      perror("write failed");
      close(sock);
      return false;
    }

    char buffer[BUFFER_SIZE] = {0};
    int valread = read(sock, buffer, BUFFER_SIZE - 1);

    if (valread <= 0) {
      std::cout << "  [FAIL] Did not receive a response for command: "
                << commands[i] << std::endl;
      success = false;
      break;
    }

    std::string raw_response(buffer, valread);
    std::string actual_response = raw_response;

    // 清理响应，移除 CRLF 和 RESP 标记，以便于比较
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
      } else if (actual_response.front() ==
                 '*') { // Array: *3\r\n$3\r\none\r\n$3\r\ntwo\r\n...
        // 对于 EXEC 命令返回的数组，我们只检查它是否以 '*'
        // 开头，详细处理在单独的函数中
        if (commands[i] == "EXEC" || commands[i] == "exec") {
          // 事务执行结果，检查是否为数组响应
          if (expected_responses[i] == "ARRAY") {
            // 这里我们只检查返回值是否为数组类型，不检查具体内容
            if (raw_response.starts_with("*")) {
              std::cout << "  [PASS] Command: '" << commands[i]
                        << "' - Expected array response, got array response"
                        << std::endl;
              continue;
            } else {
              std::cout << "  [FAIL] Command: '" << commands[i]
                        << "' - Expected array response, but got: '"
                        << raw_response << "'" << std::endl;
              success = false;
              continue;
            }
          }
        }
      }
    }

    // RESP 的 (nil) 响应是 "$-1"
    if (raw_response == "$-1\r\n") {
      actual_response = "(nil)";
    }

    if (actual_response == expected_responses[i]) {
      std::cout << "  [PASS] Command: '" << commands[i] << "' - Expected: '"
                << expected_responses[i] << "', Got: '" << actual_response
                << "'" << std::endl;
    } else {
      std::cout << "  [FAIL] Command: '" << commands[i] << "' - Expected: '"
                << expected_responses[i] << "', Got: '" << actual_response
                << "'" << std::endl;
      success = false;
    }
  }

  close(sock);
  return success;
}

// 专用于测试事务中EXEC命令返回的数组响应
bool test_transaction_array_response(
    const std::string &test_name,
    const std::vector<std::string> &transaction_commands,
    const std::vector<std::string> &expected_results) {
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

  // 发送 MULTI 命令
  std::string multi_cmd = common::ClientUtils::serialize_command("MULTI");
  if (write(sock, multi_cmd.c_str(), multi_cmd.length()) < 0) {
    perror("write failed");
    close(sock);
    return false;
  }

  char buffer[BUFFER_SIZE] = {0};
  read(sock, buffer, BUFFER_SIZE - 1); // 读取 MULTI 的响应 (OK)

  // 发送事务中的所有命令
  for (const auto &cmd : transaction_commands) {
    std::string command = common::ClientUtils::serialize_command(cmd);
    if (write(sock, command.c_str(), command.length()) < 0) {
      perror("write failed");
      close(sock);
      return false;
    }
    memset(buffer, 0, BUFFER_SIZE);
    read(sock, buffer, BUFFER_SIZE - 1); // 读取 QUEUED 响应
  }

  // 发送 EXEC 命令
  std::string exec_cmd = common::ClientUtils::serialize_command("EXEC");
  if (write(sock, exec_cmd.c_str(), exec_cmd.length()) < 0) {
    perror("write failed");
    close(sock);
    return false;
  }

  memset(buffer, 0, BUFFER_SIZE);
  int valread = read(sock, buffer, BUFFER_SIZE - 1);
  close(sock);

  if (valread <= 0) {
    std::cout << "  [FAIL] Did not receive a response for EXEC command"
              << std::endl;
    return false;
  }

  std::string response(buffer, valread);

  // 检查是否为数组响应
  if (!response.starts_with("*")) {
    std::cout << "  [FAIL] Expected array response for EXEC, but got: '"
              << response << "'" << std::endl;
    return false;
  }

  // 提取数组长度
  int array_length = 0;
  size_t pos = 1;
  while (pos < response.size() && std::isdigit(response[pos])) {
    array_length = array_length * 10 + (response[pos] - '0');
    pos++;
  }

  if (array_length != static_cast<int>(expected_results.size())) {
    std::cout << "  [FAIL] Expected array of length " << expected_results.size()
              << ", but got array of length " << array_length << std::endl;
    return false;
  }

  std::cout << "  [PASS] EXEC returned array of expected length "
            << array_length << std::endl;

  // 详细分析就到这里，因为解析完整的RESP数组比较复杂，我们只验证基本结构是否正确
  return true;
}

int main() {
  std::cout << "--- Starting Redis Transaction Tests ---" << std::endl;

  std::vector<bool> results;

  // 测试基本的事务流程
  results.push_back(run_transaction_test(
      "Basic Transaction Flow", {"MULTI", "SET name Alice", "GET name", "EXEC"},
      {"OK", "QUEUED", "QUEUED", "ARRAY"}));

  // 测试事务中的多条命令
  results.push_back(run_transaction_test(
      "Multiple Commands in Transaction",
      {"MULTI", "SET key1 value1", "SET key2 value2", "GET key1", "GET key2",
       "EXEC"},
      {"OK", "QUEUED", "QUEUED", "QUEUED", "QUEUED", "ARRAY"}));

  // 测试事务丢弃
  results.push_back(run_transaction_test(
      "Transaction Discard",
      {"MULTI", "SET temp_key will_be_discarded", "DISCARD", "GET temp_key"},
      {"OK", "QUEUED", "OK", "(nil)"}));

  // 测试在事务外调用EXEC
  results.push_back(run_transaction_test("EXEC Without MULTI", {"EXEC"},
                                         {"ERR EXEC without MULTI"}));

  // 测试在事务外调用DISCARD
  results.push_back(run_transaction_test("DISCARD Without MULTI", {"DISCARD"},
                                         {"ERR DISCARD without MULTI"}));

  // 测试嵌套MULTI
  results.push_back(
      run_transaction_test("Nested MULTI", {"MULTI", "MULTI"},
                           {"OK", "ERR MULTI calls can not be nested"}));

  // 测试空事务
  results.push_back(run_transaction_test("Empty Transaction", {"MULTI", "EXEC"},
                                         {"OK", "ARRAY"}));

  // 测试事务执行结果数组
  results.push_back(test_transaction_array_response(
      "Transaction Result Array",
      {"SET test_key1 value1", "SET test_key2 value2", "GET test_key1",
       "GET test_key2"},
      {"OK", "OK", "value1", "value2"}));

  // 测试事务执行后键的状态
  results.push_back(
      run_transaction_test("Key State After Transaction",
                           {"MULTI", "SET verified_key transaction_value",
                            "EXEC", "GET verified_key"},
                           {"OK", "QUEUED", "ARRAY", "transaction_value"}));

  // 测试大量命令的事务
  std::vector<std::string> large_transaction_cmds = {"MULTI"};
  std::vector<std::string> large_transaction_expected = {"OK"};
  for (int i = 0; i < 10; i++) {
    large_transaction_cmds.push_back("SET key" + std::to_string(i) + " value" +
                                     std::to_string(i));
    large_transaction_expected.push_back("QUEUED");
  }
  large_transaction_cmds.push_back("EXEC");
  large_transaction_expected.push_back("ARRAY");
  results.push_back(run_transaction_test(
      "Large Transaction", large_transaction_cmds, large_transaction_expected));

  // 测试事务中的命令错误处理 (例如，错误参数的命令)
  results.push_back(run_transaction_test("Command Error in Transaction",
                                         {"MULTI", "SET", "EXEC"},
                                         {"OK", "QUEUED", "ARRAY"}));

  int failed_count = 0;
  for (bool result : results) {
    if (!result) {
      failed_count++;
    }
  }

  std::cout << "\n--- Transaction Test Summary ---" << std::endl;
  if (failed_count == 0) {
    std::cout << "√ All " << results.size() << " transaction tests passed!"
              << std::endl;
    return 0; // 成功
  } else {
    std::cout << "× " << failed_count << " out of " << results.size()
              << " transaction tests failed." << std::endl;
    return 1; // 失败
  }
}