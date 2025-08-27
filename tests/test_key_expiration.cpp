#include <cassert>
#include <chrono>
#include <functional>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

import resp;
import timer;
import kv_server;
import logger;

// 测试辅助宏
#define TEST_ASSERT(condition, message)                                        \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << "断言失败: " << message << " 在 " << __FILE__ << " 行 "    \
                << __LINE__ << std::endl;                                      \
      return false;                                                            \
    }                                                                          \
  } while (0)

// 创建命令辅助函数
resp::RespValue create_command(const std::vector<std::string> &parts) {
  auto arr = std::make_unique<resp::RespArray>();

  for (const auto &part : parts) {
    resp::RespBulkString item;
    item.value = part;
    arr->values.push_back(item);
  }

  return resp::RespValue(std::move(arr));
}

// 测试 EXPIRE 命令
bool test_expire_command() {
  std::cout << "测试 EXPIRE 命令..." << std::endl;

  KVServer server;
  TimerQueue timer_queue;
  server.set_timer_queue(&timer_queue);

  // 设置一个键
  auto set_cmd = create_command({"SET", "test_key", "test_value"});
  std::string set_result = server.execute_command(set_cmd);
  TEST_ASSERT(set_result == resp::serialize_ok(), "SET 命令应该成功");

  // 设置过期时间为 5 秒
  auto expire_cmd = create_command({"EXPIRE", "test_key", "5"});
  std::string expire_result = server.execute_command(expire_cmd);
  TEST_ASSERT(expire_result == resp::serialize_integer(1),
              "EXPIRE 命令应该成功返回 1");

  // 检查 TTL
  auto ttl_cmd = create_command({"TTL", "test_key"});
  std::string ttl_result = server.execute_command(ttl_cmd);
  // TTL 应该接近 5，但可能会有微小的差异
  TEST_ASSERT(ttl_result == resp::serialize_integer(5) ||
                  ttl_result == resp::serialize_integer(4),
              "TTL 应该返回接近 5 的值");

  // 对不存在的键设置过期时间
  auto expire_non_existing_cmd =
      create_command({"EXPIRE", "non_existing_key", "10"});
  std::string expire_non_existing_result =
      server.execute_command(expire_non_existing_cmd);
  TEST_ASSERT(expire_non_existing_result == resp::serialize_integer(0),
              "对不存在的键设置过期时间应该返回 0");

  std::cout << "EXPIRE 命令测试通过！" << std::endl;
  return true;
}

// 测试 PEXPIRE 命令
bool test_pexpire_command() {
  std::cout << "测试 PEXPIRE 命令..." << std::endl;

  KVServer server;
  TimerQueue timer_queue;
  server.set_timer_queue(&timer_queue);

  // 设置一个键
  auto set_cmd = create_command({"SET", "test_key", "test_value"});
  std::string set_result = server.execute_command(set_cmd);
  TEST_ASSERT(set_result == resp::serialize_ok(), "SET 命令应该成功");

  // 设置过期时间为 5000 毫秒
  auto pexpire_cmd = create_command({"PEXPIRE", "test_key", "5000"});
  std::string pexpire_result = server.execute_command(pexpire_cmd);
  TEST_ASSERT(pexpire_result == resp::serialize_integer(1),
              "PEXPIRE 命令应该成功返回 1");

  // 检查 PTTL
  auto pttl_cmd = create_command({"PTTL", "test_key"});
  std::string pttl_result = server.execute_command(pttl_cmd);
  // PTTL 应该接近 5000，但会有一些差异
  long long pttl_value = 0;
  if (pttl_result.substr(0, 1) == ":") {
    pttl_value = std::stoll(pttl_result.substr(1, pttl_result.size() - 3));
  }
  TEST_ASSERT(pttl_value > 4000 && pttl_value <= 5000,
              "PTTL 应该返回接近 5000 的值");

  // 检查 TTL (秒级)
  auto ttl_cmd = create_command({"TTL", "test_key"});
  std::string ttl_result = server.execute_command(ttl_cmd);
  // TTL 应该接近 5，但可能会有微小的差异
  TEST_ASSERT(ttl_result == resp::serialize_integer(5) ||
                  ttl_result == resp::serialize_integer(4),
              "TTL 应该返回接近 5 的值");

  std::cout << "PEXPIRE 命令测试通过！" << std::endl;
  return true;
}

// 测试 PERSIST 命令
bool test_persist_command() {
  std::cout << "测试 PERSIST 命令..." << std::endl;

  KVServer server;
  TimerQueue timer_queue;
  server.set_timer_queue(&timer_queue);

  // 设置一个键
  auto set_cmd = create_command({"SET", "test_key", "test_value"});
  std::string set_result = server.execute_command(set_cmd);
  TEST_ASSERT(set_result == resp::serialize_ok(), "SET 命令应该成功");

  // 设置过期时间
  auto expire_cmd = create_command({"EXPIRE", "test_key", "10"});
  std::string expire_result = server.execute_command(expire_cmd);
  TEST_ASSERT(expire_result == resp::serialize_integer(1),
              "EXPIRE 命令应该成功返回 1");

  // 检查 TTL
  auto ttl_cmd = create_command({"TTL", "test_key"});
  std::string ttl_result = server.execute_command(ttl_cmd);
  TEST_ASSERT(ttl_result != resp::serialize_integer(-1), "TTL 应该不是 -1");

  // 移除过期时间
  auto persist_cmd = create_command({"PERSIST", "test_key"});
  std::string persist_result = server.execute_command(persist_cmd);
  TEST_ASSERT(persist_result == resp::serialize_integer(1),
              "PERSIST 命令应该成功返回 1");

  // 再次检查 TTL
  ttl_result = server.execute_command(ttl_cmd);
  TEST_ASSERT(ttl_result == resp::serialize_integer(-1),
              "移除过期时间后 TTL 应该是 -1");

  // 对不存在过期时间的键使用 PERSIST
  persist_result = server.execute_command(persist_cmd);
  TEST_ASSERT(persist_result == resp::serialize_integer(0),
              "对不存在过期时间的键使用 PERSIST 应该返回 0");

  // 对不存在的键使用 PERSIST
  auto persist_non_existing_cmd =
      create_command({"PERSIST", "non_existing_key"});
  std::string persist_non_existing_result =
      server.execute_command(persist_non_existing_cmd);
  TEST_ASSERT(persist_non_existing_result == resp::serialize_integer(0),
              "对不存在的键使用 PERSIST 应该返回 0");

  std::cout << "PERSIST 命令测试通过！" << std::endl;
  return true;
}

// 测试惰性删除机制
bool test_lazy_deletion() {
  std::cout << "测试惰性删除机制..." << std::endl;

  KVServer server;
  TimerQueue timer_queue;
  server.set_timer_queue(&timer_queue);

  // 设置一个键
  auto set_cmd = create_command({"SET", "test_key", "test_value"});
  std::string set_result = server.execute_command(set_cmd);
  TEST_ASSERT(set_result == resp::serialize_ok(), "SET 命令应该成功");

  // 设置一个短的过期时间（1秒）
  auto expire_cmd = create_command({"EXPIRE", "test_key", "1"});
  std::string expire_result = server.execute_command(expire_cmd);
  TEST_ASSERT(expire_result == resp::serialize_integer(1),
              "EXPIRE 命令应该成功返回 1");

  // 等待过期
  std::this_thread::sleep_for(std::chrono::milliseconds(1200));

  // 尝试获取键
  auto get_cmd = create_command({"GET", "test_key"});
  std::string get_result = server.execute_command(get_cmd);
  TEST_ASSERT(get_result == resp::serialize_null_bulk_string(),
              "过期后 GET 命令应该返回 nil");

  // 检查 TTL
  auto ttl_cmd = create_command({"TTL", "test_key"});
  std::string ttl_result = server.execute_command(ttl_cmd);
  TEST_ASSERT(ttl_result == resp::serialize_integer(-2),
              "过期后 TTL 应该返回 -2（键不存在）");

  std::cout << "惰性删除机制测试通过！" << std::endl;
  return true;
}

// 测试定期删除机制
bool test_periodic_deletion() {
  std::cout << "测试定期删除机制..." << std::endl;

  KVServer server;
  TimerQueue timer_queue;
  server.set_timer_queue(&timer_queue);

  // 设置多个键，使得过期时间各不相同
  for (int i = 1; i <= 30; ++i) {
    std::string key = "key_" + std::to_string(i);
    std::string value = "value_" + std::to_string(i);

    // 设置键
    auto set_cmd = create_command({"SET", key, value});
    std::string set_result = server.execute_command(set_cmd);
    TEST_ASSERT(set_result == resp::serialize_ok(), "SET 命令应该成功");

    // 设置不同的过期时间
    if (i <= 10) { // 前10个键设置为1秒后过期
      auto expire_cmd = create_command({"EXPIRE", key, "1"});
      server.execute_command(expire_cmd);
    } else if (i <= 20) { // 中间10个键设置为5秒后过期
      auto expire_cmd = create_command({"EXPIRE", key, "5"});
      server.execute_command(expire_cmd);
    }
    // 最后10个键不设置过期时间
  }

  // 等待1.2秒，让前10个键过期
  std::this_thread::sleep_for(std::chrono::milliseconds(1200));

  // 触发定期删除
  timer_queue.process_timer_event();

  // 检查前10个键是否已被删除
  for (int i = 1; i <= 10; ++i) {
    std::string key = "key_" + std::to_string(i);
    auto get_cmd = create_command({"GET", key});
    std::string get_result = server.execute_command(get_cmd);
    TEST_ASSERT(get_result == resp::serialize_null_bulk_string(),
                "定期删除后过期键应该被删除");
  }

  // 检查中间10个键是否仍然存在
  for (int i = 11; i <= 20; ++i) {
    std::string key = "key_" + std::to_string(i);
    auto get_cmd = create_command({"GET", key});
    std::string get_result = server.execute_command(get_cmd);
    TEST_ASSERT(get_result != resp::serialize_null_bulk_string(),
                "未过期的键不应被删除");
  }

  std::cout << "定期删除机制测试通过！" << std::endl;
  return true;
}

// 全局测试
bool test_set_with_expire_overwrite() {
  std::cout << "测试 SET 覆盖过期时间..." << std::endl;

  KVServer server;
  TimerQueue timer_queue;
  server.set_timer_queue(&timer_queue);

  // 设置一个键
  auto set_cmd = create_command({"SET", "test_key", "original_value"});
  std::string set_result = server.execute_command(set_cmd);
  TEST_ASSERT(set_result == resp::serialize_ok(), "SET 命令应该成功");

  // 设置过期时间
  auto expire_cmd = create_command({"EXPIRE", "test_key", "10"});
  std::string expire_result = server.execute_command(expire_cmd);
  TEST_ASSERT(expire_result == resp::serialize_integer(1),
              "EXPIRE 命令应该成功返回 1");

  // 检查 TTL
  auto ttl_cmd = create_command({"TTL", "test_key"});
  std::string ttl_result = server.execute_command(ttl_cmd);
  TEST_ASSERT(ttl_result != resp::serialize_integer(-1), "TTL 应该不是 -1");

  // 重新 SET 同一个键
  auto reset_cmd = create_command({"SET", "test_key", "new_value"});
  std::string reset_result = server.execute_command(reset_cmd);
  TEST_ASSERT(reset_result == resp::serialize_ok(), "SET 命令应该成功");

  // 检查值是否已更新
  auto get_cmd = create_command({"GET", "test_key"});
  std::string get_result = server.execute_command(get_cmd);
  TEST_ASSERT(get_result == resp::serialize_bulk_string("new_value"),
              "值应该已被更新");

  // 检查 TTL - 应该被清除
  ttl_result = server.execute_command(ttl_cmd);
  TEST_ASSERT(ttl_result == resp::serialize_integer(-1),
              "重新 SET 后 TTL 应该是 -1");

  std::cout << "SET 覆盖过期时间测试通过！" << std::endl;
  return true;
}

// 集成测试 - 测试所有功能协同工作
bool test_integration() {
  std::cout << "执行集成测试..." << std::endl;

  KVServer server;
  TimerQueue timer_queue;
  server.set_timer_queue(&timer_queue);

  // 设置三个键
  for (int i = 1; i <= 3; ++i) {
    std::string key = "key_" + std::to_string(i);
    std::string value = "value_" + std::to_string(i);
    auto set_cmd = create_command({"SET", key, value});
    server.execute_command(set_cmd);
  }

  // 为第一个键设置短过期时间
  auto expire_cmd1 = create_command({"EXPIRE", "key_1", "1"});
  server.execute_command(expire_cmd1);

  // 为第二个键设置长过期时间
  auto expire_cmd2 = create_command({"EXPIRE", "key_2", "10"});
  server.execute_command(expire_cmd2);

  // 第三个键不设置过期时间

  // 等待第一个键过期
  std::this_thread::sleep_for(std::chrono::milliseconds(1200));

  // 检查第一个键 - 应该已过期
  auto get_cmd1 = create_command({"GET", "key_1"});
  std::string get_result1 = server.execute_command(get_cmd1);
  TEST_ASSERT(get_result1 == resp::serialize_null_bulk_string(),
              "key_1 应该已过期");

  // 检查第二个键 - 应该还存在
  auto get_cmd2 = create_command({"GET", "key_2"});
  std::string get_result2 = server.execute_command(get_cmd2);
  TEST_ASSERT(get_result2 == resp::serialize_bulk_string("value_2"),
              "key_2 应该仍然存在");

  // 移除第二个键的过期时间
  auto persist_cmd = create_command({"PERSIST", "key_2"});
  std::string persist_result = server.execute_command(persist_cmd);
  TEST_ASSERT(persist_result == resp::serialize_integer(1), "PERSIST 应该成功");

  // 检查第二个键的 TTL
  auto ttl_cmd = create_command({"TTL", "key_2"});
  std::string ttl_result = server.execute_command(ttl_cmd);
  TEST_ASSERT(ttl_result == resp::serialize_integer(-1),
              "移除过期时间后 TTL 应该是 -1");

  // 触发定期删除
  timer_queue.process_timer_event();

  // 重新检查所有键
  get_result1 = server.execute_command(get_cmd1);
  TEST_ASSERT(get_result1 == resp::serialize_null_bulk_string(),
              "key_1 应该被定期删除");

  get_result2 = server.execute_command(get_cmd2);
  TEST_ASSERT(get_result2 == resp::serialize_bulk_string("value_2"),
              "key_2 移除过期时间后不应被删除");

  auto get_cmd3 = create_command({"GET", "key_3"});
  std::string get_result3 = server.execute_command(get_cmd3);
  TEST_ASSERT(get_result3 == resp::serialize_bulk_string("value_3"),
              "未设置过期时间的 key_3 不应被删除");

  std::cout << "集成测试通过！" << std::endl;
  return true;
}

int main() {
  // 设置日志级别
  Logger::instance().set_level(LogLevel::INFO);
  std::cout << "开始键过期功能测试..." << std::endl;

  bool all_passed = true;
  std::vector<std::pair<std::string, std::function<bool()>>> tests = {
      {"EXPIRE 命令测试", test_expire_command},
      {"PEXPIRE 命令测试", test_pexpire_command},
      {"PERSIST 命令测试", test_persist_command},
      {"惰性删除机制测试", test_lazy_deletion},
      {"定期删除机制测试", test_periodic_deletion},
      {"SET 覆盖过期时间测试", test_set_with_expire_overwrite},
      {"集成测试", test_integration}};

  int passed = 0;
  int failed = 0;

  for (const auto &[name, test_func] : tests) {
    std::cout << "\n===================================" << std::endl;
    std::cout << "执行测试: " << name << std::endl;
    std::cout << "===================================" << std::endl;

    try {
      if (test_func()) {
        std::cout << "√ 测试通过: " << name << std::endl;
        passed++;
      } else {
        std::cerr << "× 测试失败: " << name << std::endl;
        all_passed = false;
        failed++;
      }
    } catch (const std::exception &e) {
      std::cerr << "× 测试出现异常: " << name << " - " << e.what()
                << std::endl;
      all_passed = false;
      failed++;
    }
  }

  std::cout << "\n===================================" << std::endl;
  std::cout << "测试结果摘要:" << std::endl;
  std::cout << "总计: " << tests.size() << " 个测试" << std::endl;
  std::cout << "通过: " << passed << " 个测试" << std::endl;
  std::cout << "失败: " << failed << " 个测试" << std::endl;
  std::cout << "===================================" << std::endl;

  return all_passed ? 0 : 1;
}