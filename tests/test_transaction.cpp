#include <atomic>
#include <cassert>
#include <chrono>
#include <format>
#include <iostream>
#include <thread>
#include <unistd.h>
#include <vector>

import kv_server;
import aof;
import timer;
import resp;

// 将响应数据转换为人类可读的格式
std::string prettify_response(const std::string &resp_data) {
  if (resp_data.empty())
    return "(empty)";

  if (resp_data[0] == '+') {
    return resp_data.substr(1, resp_data.size() - 3); // 移除 '+' 和 '\r\n'
  } else if (resp_data[0] == '-') {
    return std::format("ERROR: {}", resp_data.substr(1, resp_data.size() - 3));
  } else if (resp_data[0] == ':') {
    return resp_data.substr(1, resp_data.size() - 3);
  } else if (resp_data[0] == '$') {
    if (resp_data == "$-1\r\n") {
      return "(nil)";
    }
    auto first_crlf = resp_data.find("\r\n");
    auto content_start = first_crlf + 2;
    return resp_data.substr(content_start,
                            resp_data.size() - content_start - 2);
  } else if (resp_data[0] == '*') {
    std::string result = "Array:";
    std::string_view view = resp_data;
    view.remove_prefix(1); // 跳过 '*'

    // 找到数组大小
    int size = 0;
    size_t pos = 0;
    while (pos < view.size() && std::isdigit(view[pos])) {
      size = size * 10 + (view[pos] - '0');
      pos++;
    }
    view.remove_prefix(pos + 2); // 跳过大小和 "\r\n"

    // 解析每个元素
    for (int i = 0; i < size; i++) {
      if (view[0] == '$') {
        auto bulk_len_end = view.find("\r\n");
        auto bulk_len_str = view.substr(1, bulk_len_end - 1);
        int bulk_len = std::stoi(std::string(bulk_len_str));

        if (bulk_len == -1) {
          result += "\n  - (nil)";
          view.remove_prefix(bulk_len_end + 2);
        } else {
          view.remove_prefix(bulk_len_end + 2);
          result += std::format("\n  - {}", view.substr(0, bulk_len));
          view.remove_prefix(bulk_len + 2);
        }
      } else if (view[0] == '+' || view[0] == '-' || view[0] == ':') {
        auto line_end = view.find("\r\n");
        result += std::format("\n  - {}", view.substr(1, line_end - 1));
        view.remove_prefix(line_end + 2);
      }
    }
    return result;
  }

  return resp_data; // 默认情况下原样返回
}

// 测试事务基本功能
bool test_basic_transaction() {
  std::cout << "测试事务的基本功能..." << std::endl;

  KVServer server;

  // 创建命令
  auto create_set_command = [](const std::string &key,
                               const std::string &value) {
    auto array = std::make_unique<resp::RespArray>();
    array->values.push_back(resp::RespBulkString{std::string("SET")});
    array->values.push_back(resp::RespBulkString{std::string(key)});
    array->values.push_back(resp::RespBulkString{std::string(value)});
    return resp::RespValue{std::move(array)};
  };

  auto create_get_command = [](const std::string &key) {
    auto array = std::make_unique<resp::RespArray>();
    array->values.push_back(resp::RespBulkString{std::string("GET")});
    array->values.push_back(resp::RespBulkString{std::string(key)});
    return resp::RespValue{std::move(array)};
  };

  // 创建事务中的命令
  std::vector<resp::RespValue> transaction;
  transaction.push_back(create_set_command("tx_key1", "value1"));
  transaction.push_back(create_set_command("tx_key2", "value2"));
  transaction.push_back(create_get_command("tx_key1"));

  // 执行事务
  std::string result = server.execute_transaction(transaction);
  std::cout << "事务执行结果: " << prettify_response(result) << std::endl;

  // 验证事务执行后的状态
  std::string get_result1 =
      server.execute_command(create_get_command("tx_key1"), false);
  std::string get_result2 =
      server.execute_command(create_get_command("tx_key2"), false);

  bool success = true;

  if (prettify_response(get_result1) != "value1") {
    std::cout << "错误: tx_key1 的值不正确: " << prettify_response(get_result1)
              << std::endl;
    success = false;
  }

  if (prettify_response(get_result2) != "value2") {
    std::cout << "错误: tx_key2 的值不正确: " << prettify_response(get_result2)
              << std::endl;
    success = false;
  }

  if (success) {
    std::cout << "基本事务测试通过!" << std::endl;
  } else {
    std::cout << "基本事务测试失败!" << std::endl;
  }

  return success;
}

// 测试空事务
bool test_empty_transaction() {
  std::cout << "\n测试空事务..." << std::endl;

  KVServer server;
  std::vector<resp::RespValue> empty_transaction;

  std::string result = server.execute_transaction(empty_transaction);
  std::cout << "空事务结果: " << prettify_response(result) << std::endl;

  // 空事务应当返回空数组
  bool success = result == "*0\r\n";

  if (success) {
    std::cout << "空事务测试通过!" << std::endl;
  } else {
    std::cout << "空事务测试失败! 期望: *0\\r\\n, 得到: " << result
              << std::endl;
  }

  return success;
}

// 测试命令错误处理
bool test_transaction_error_handling() {
  std::cout << "\n测试事务中的错误命令处理..." << std::endl;

  KVServer server;

  // 创建一个错误命令（SET 缺少参数）
  auto create_error_command = []() {
    auto array = std::make_unique<resp::RespArray>();
    array->values.push_back(resp::RespBulkString{std::string("SET")});
    // 缺少 key 和 value 参数
    return resp::RespValue{std::move(array)};
  };

  auto create_valid_command = []() {
    auto array = std::make_unique<resp::RespArray>();
    array->values.push_back(resp::RespBulkString{std::string("SET")});
    array->values.push_back(resp::RespBulkString{std::string("valid_key")});
    array->values.push_back(resp::RespBulkString{std::string("valid_value")});
    return resp::RespValue{std::move(array)};
  };

  auto create_get_command = [](const std::string &key) {
    auto array = std::make_unique<resp::RespArray>();
    array->values.push_back(resp::RespBulkString{std::string("GET")});
    array->values.push_back(resp::RespBulkString{std::string(key)});
    return resp::RespValue{std::move(array)};
  };

  // 创建包含错误命令的事务
  std::vector<resp::RespValue> transaction;
  transaction.push_back(create_valid_command()); // 有效命令
  transaction.push_back(create_error_command()); // 无效命令
  transaction.push_back(create_valid_command()); // 有效命令

  // 执行事务
  std::string result = server.execute_transaction(transaction);
  std::cout << "带错误命令的事务执行结果: " << prettify_response(result)
            << std::endl;

  // 验证：即使有错误的命令，其他命令也应该被执行
  std::string get_result =
      server.execute_command(create_get_command("valid_key"), false);

  bool success = prettify_response(get_result) == "valid_value";

  if (success) {
    std::cout << "错误命令处理测试通过!" << std::endl;
  } else {
    std::cout << "错误命令处理测试失败!" << std::endl;
  }

  return success;
}

// 测试大型事务
bool test_large_transaction() {
  std::cout << "\n测试大型事务..." << std::endl;

  KVServer server;

  // 创建命令
  auto create_set_command = [](const std::string &key,
                               const std::string &value) {
    auto array = std::make_unique<resp::RespArray>();
    array->values.push_back(resp::RespBulkString{std::string("SET")});
    array->values.push_back(resp::RespBulkString{std::string(key)});
    array->values.push_back(resp::RespBulkString{std::string(value)});
    return resp::RespValue{std::move(array)};
  };

  auto create_get_command = [](const std::string &key) {
    auto array = std::make_unique<resp::RespArray>();
    array->values.push_back(resp::RespBulkString{std::string("GET")});
    array->values.push_back(resp::RespBulkString{std::string(key)});
    return resp::RespValue{std::move(array)};
  };

  // 创建一个包含100个命令的大型事务
  std::vector<resp::RespValue> transaction;

  for (int i = 0; i < 50; i++) {
    transaction.push_back(create_set_command("large_key" + std::to_string(i),
                                             "value" + std::to_string(i)));
  }

  for (int i = 0; i < 50; i++) {
    transaction.push_back(create_get_command("large_key" + std::to_string(i)));
  }

  // 执行事务
  auto start = std::chrono::steady_clock::now();
  std::string result = server.execute_transaction(transaction);
  auto end = std::chrono::steady_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  std::cout << "大型事务执行时间: " << duration.count() << "ms" << std::endl;

  // 验证事务执行后的几个键
  bool success = true;
  for (int i = 0; i < 50; i += 10) {
    std::string get_result = server.execute_command(
        create_get_command("large_key" + std::to_string(i)), false);

    if (prettify_response(get_result) != "value" + std::to_string(i)) {
      std::cout << "错误: large_key" << i << " 的值不正确" << std::endl;
      success = false;
      break;
    }
  }

  if (success) {
    std::cout << "大型事务测试通过!" << std::endl;
  } else {
    std::cout << "大型事务测试失败!" << std::endl;
  }

  return success;
}

int main() {
  std::cout << "---- 开始事务单元测试 ----" << std::endl;

  bool all_tests_passed = true;

  all_tests_passed &= test_basic_transaction();
  all_tests_passed &= test_empty_transaction();
  all_tests_passed &= test_transaction_error_handling();
  all_tests_passed &= test_large_transaction();

  std::cout << "\n---- 测试结果摘要 ----" << std::endl;
  if (all_tests_passed) {
    std::cout << "√ 所有测试通过!" << std::endl;
    return 0;
  } else {
    std::cout << "× 部分测试失败!" << std::endl;
    return 1;
  }
}