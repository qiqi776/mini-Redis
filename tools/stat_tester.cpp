#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>

import client_utils;

const int PORT = 6379;
const char *IP = "127.0.0.1";
const int BUFFER_SIZE = 2048; // 为 INFO 命令增加缓冲区大小

// --- 辅助函数 ---

// 连接服务器，发送单个命令并返回原始响应。
std::string send_command_and_get_response(const std::string &command) {
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock == -1) {
    perror("socket failed");
    return "FAIL";
  }

  sockaddr_in serv_addr;
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(PORT);

  if (inet_pton(AF_INET, IP, &serv_addr.sin_addr) <= 0) {
    perror("inet_pton failed");
    close(sock);
    return "FAIL";
  }

  if (connect(sock, (sockaddr *)&serv_addr, sizeof(serv_addr)) == -1) {
    perror("connect failed");
    close(sock);
    return "FAIL";
  }

  // 使用通用工具序列化命令
  std::string message = common::ClientUtils::serialize_command(command);
  if (write(sock, message.c_str(), message.length()) < 0) {
    perror("write failed");
    close(sock);
    return "FAIL";
  }

  char buffer[BUFFER_SIZE] = {0};
  int valread = read(sock, buffer, BUFFER_SIZE - 1);
  close(sock);

  if (valread <= 0) {
    return "";
  }

  return std::string(buffer, valread);
}

// 解析 RESP 响应，将其转换为用于比较的简单字符串。
void parse_resp_response(std::string &actual_response) {
  if (actual_response.empty())
    return;
  // 移除末尾的 \r\n
  while (!actual_response.empty() &&
         (actual_response.back() == '\n' || actual_response.back() == '\r')) {
    actual_response.pop_back();
  }
  if (actual_response.empty())
    return;

  // 将不同的 RESP 类型转换为简单字符串
  if (actual_response.front() == '+') { // 简单字符串: +OK
    actual_response = actual_response.substr(1);
  } else if (actual_response.front() == '-') { // 错误: -ERR message
    actual_response = actual_response.substr(1);
  } else if (actual_response.front() == '$') { // 批量字符串
    // 空批量字符串 "$-1\r\n"
    if (actual_response.rfind("$-1", 0) == 0) {
      actual_response = "(nil)";
      return;
    }
    // 普通批量字符串，例如 "$4\r\nvall\r\n"
    size_t first_crlf = actual_response.find("\r\n");
    if (first_crlf != std::string::npos) {
      actual_response = actual_response.substr(first_crlf + 2);
    }
  }
}

// 解析 INFO 命令的输出，将其存储在 map 中。
std::unordered_map<std::string, std::string>
parse_info(const std::string &info_str) {
  std::unordered_map<std::string, std::string> info_map;
  std::string_view s(info_str);
  // 找到批量字符串头后的实际内容起点
  size_t start_pos = s.find("\r\n");
  if (start_pos == std::string::npos) {
    return info_map;
  }
  s.remove_prefix(start_pos + 2);

  size_t pos = 0;
  while ((pos = s.find("\r\n")) != std::string_view::npos) {
    std::string_view line = s.substr(0, pos);
    s.remove_prefix(pos + 2);
    if (line.empty() || line[0] == '#') {
      continue;
    }
    size_t colon_pos = line.find(':');
    if (colon_pos != std::string_view::npos) {
      std::string key(line.substr(0, colon_pos));
      std::string value(line.substr(colon_pos + 1));
      info_map[key] = value;
    }
  }
  return info_map;
}

// 从 INFO 的 keyspace 字符串中提取键的数量。
int parse_keyspace_keys(const std::string &keyspace_str) {
  auto pos = keyspace_str.find("keys=");
  if (pos == std::string::npos)
    return -1;
  pos += 5; // 跳过 "keys="
  auto end_pos = keyspace_str.find(',', pos);
  return std::stoi(std::string(keyspace_str.substr(pos, end_pos - pos)));
}

// 运行单个命令测试并验证其响应。
void run_test(const std::string &test_name, const std::string &command,
              const std::string &expected_response) {
  std::cout << "Running test: " << test_name << "..." << std::endl;
  std::string response = send_command_and_get_response(command);
  parse_resp_response(response);
  if (response == expected_response) {
    std::cout << "  [PASS] Expected: '" << expected_response << "', Got: '"
              << response << "'" << std::endl;
  } else {
    std::cerr << "  [FAIL] Expected: '" << expected_response << "', Got: '"
              << response << "'" << std::endl;
    exit(1);
  }
}

// --- 主测试逻辑 ---

int main() {
  // 等待服务器启动，确保测试时服务器可用。
  std::this_thread::sleep_for(std::chrono::seconds(1));

  std::cout << "--- Starting Statistics Verifier ---" << std::endl;

  // 0. 获取初始状态，以确保测试不受先前状态的影响。
  std::string initial_info_raw = send_command_and_get_response("INFO");
  auto initial_info = parse_info(initial_info_raw);
  long long initial_commands =
      initial_info.count("total_commands_processed")
          ? std::stoll(initial_info["total_commands_processed"])
          : 0;
  long long initial_hits = initial_info.count("keyspace_hits")
                               ? std::stoll(initial_info["keyspace_hits"])
                               : 0;
  long long initial_misses = initial_info.count("keyspace_misses")
                                 ? std::stoll(initial_info["keyspace_misses"])
                                 : 0;
  int initial_keys =
      initial_info.count("db0") ? parse_keyspace_keys(initial_info["db0"]) : 0;

  // 1. 执行一系列命令来改变服务器状态。
  run_test("Set key1", "SET key1 val1", "OK");
  run_test("Set key2", "SET key2 val2", "OK");
  run_test("Get key1", "GET key1", "val1");
  run_test("Get key2", "GET key2", "val2");
  run_test("Get non-existent key", "GET key3", "(nil)");

  // 2. 获取最终的 INFO 统计信息。
  std::cout << "Fetching server statistics..." << std::endl;
  std::string final_info_raw = send_command_and_get_response("INFO");
  auto final_info = parse_info(final_info_raw);

  bool all_passed = true;

  // 3. 验证所有统计数据是否符合预期。
  std::cout << "Verifying statistics..." << std::endl;

  // 验证处理的命令总数：初始命令数 + 5个测试命令 + 1个最终INFO命令。
  long long commands_processed =
      std::stoll(final_info["total_commands_processed"]);
  long long expected_commands = initial_commands + 6;
  if (commands_processed == expected_commands) {
    std::cout << "  [PASS] Total commands processed: " << commands_processed
              << std::endl;
  } else {
    std::cerr << "  [FAIL] Expected " << expected_commands
              << " commands processed, got: " << commands_processed
              << std::endl;
    all_passed = false;
  }

  // 验证命中次数：初始命中数 + 2次成功GET。
  long long hits = std::stoll(final_info["keyspace_hits"]);
  long long expected_hits = initial_hits + 2;
  if (hits == expected_hits) {
    std::cout << "  [PASS] Keyspace hits: " << hits << std::endl;
  } else {
    std::cerr << "  [FAIL] Expected " << expected_hits
              << " keyspace hits, got: " << hits << std::endl;
    all_passed = false;
  }

  // 验证未命中次数：初始未命中数 + 1次失败GET。
  long long misses = std::stoll(final_info["keyspace_misses"]);
  long long expected_misses = initial_misses + 1;
  if (misses == expected_misses) {
    std::cout << "  [PASS] Keyspace misses: " << misses << std::endl;
  } else {
    std::cerr << "  [FAIL] Expected " << expected_misses
              << " keyspace misses, got: " << misses << std::endl;
    all_passed = false;
  }

  // 验证键数量：初始键数 + 2个新SET。
  int num_keys = parse_keyspace_keys(final_info["db0"]);
  int expected_keys = initial_keys + 2;
  if (num_keys == expected_keys) {
    std::cout << "  [PASS] Keyspace keys: " << num_keys << std::endl;
  } else {
    std::cerr << "  [FAIL] Expected " << expected_keys
              << " keys, got: " << num_keys << std::endl;
    all_passed = false;
  }

  std::cout << "\n--- Test Summary ---" << std::endl;
  if (all_passed) {
    std::cout << "√ All statistics tests passed!" << std::endl;
    return 0;
  } else {
    std::cerr << "× Some statistics tests failed." << std::endl;
    return 1;
  }
}