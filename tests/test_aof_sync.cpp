#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <thread>

import aof;
import resp;
import logger;
import timer;

// 测试辅助宏
#define TEST_ASSERT(condition, message)                                        \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << "断言失败: " << message << " 在 " << __FILE__ << " 行 "    \
                << __LINE__ << std::endl;                                      \
      return false;                                                            \
    }                                                                          \
  } while (0)

// 创建一个测试用的RESP命令
resp::RespValue create_test_command(const std::string &key,
                                    const std::string &value) {
  // 构建SET命令: ["SET", "key", "value"]
  auto arr = std::make_unique<resp::RespArray>();

  // 添加命令名
  resp::RespBulkString cmd;
  cmd.value = "SET";
  arr->values.push_back(cmd);

  // 添加键
  resp::RespBulkString k;
  k.value = key;
  arr->values.push_back(k);

  // 添加值
  resp::RespBulkString v;
  v.value = value;
  arr->values.push_back(v);

  // 封装成RespValue
  resp::RespValue command = std::move(arr);
  return command;
}

// 测试always同步策略
bool test_always_sync_strategy() {
  std::cout << "测试always同步策略..." << std::endl;

  // 准备测试文件
  std::string test_file = "test_always.aof";
  if (std::filesystem::exists(test_file)) {
    std::filesystem::remove(test_file);
  }

  // 创建使用always策略的AOF对象
  Aof aof(test_file, AofSyncStrategy::ALWAYS);

  // 写入一系列命令
  for (int i = 1; i <= 5; i++) {
    auto cmd = create_test_command("key" + std::to_string(i),
                                   "value" + std::to_string(i));
    aof.append(cmd);
  }

  // 检查文件是否存在且内容有效
  TEST_ASSERT(std::filesystem::exists(test_file), "AOF文件未创建");
  TEST_ASSERT(std::filesystem::file_size(test_file) > 0, "AOF文件为空");

  // 加载命令并验证
  auto commands = aof.load_commands();
  TEST_ASSERT(commands.size() == 5, "加载的命令数量不正确");

  // 清理测试文件
  std::filesystem::remove(test_file);
  return true;
}

// 测试everysec同步策略
bool test_everysec_sync_strategy() {
  std::cout << "测试everysec同步策略..." << std::endl;

  // 准备测试文件
  std::string test_file = "test_everysec.aof";
  if (std::filesystem::exists(test_file)) {
    std::filesystem::remove(test_file);
  }

  // 创建使用everysec策略的AOF对象
  Aof aof(test_file, AofSyncStrategy::EVERYSEC);

  // 写入几个命令，但不调用fsync_async
  for (int i = 1; i <= 3; i++) {
    auto cmd = create_test_command("key" + std::to_string(i),
                                   "value" + std::to_string(i));
    aof.append(cmd);
  }

  // 文件应该已创建但可能未完全刷新到磁盘
  TEST_ASSERT(std::filesystem::exists(test_file), "AOF文件未创建");

  // 手动触发一次fsync操作，模拟定时器触发
  aof.fsync_async();

  // 再写入几个命令
  for (int i = 4; i <= 6; i++) {
    auto cmd = create_test_command("key" + std::to_string(i),
                                   "value" + std::to_string(i));
    aof.append(cmd);
  }

  // 再次手动触发fsync
  aof.fsync_async();

  // 加载命令并验证
  auto commands = aof.load_commands();
  TEST_ASSERT(commands.size() == 6,
              "加载的命令数量不正确，期望6个命令，实际有 " +
                  std::to_string(commands.size()) + " 个");

  // 清理测试文件
  std::filesystem::remove(test_file);
  return true;
}

// 测试no同步策略
bool test_no_sync_strategy() {
  std::cout << "测试no同步策略..." << std::endl;

  // 准备测试文件
  std::string test_file = "test_no_sync.aof";
  if (std::filesystem::exists(test_file)) {
    std::filesystem::remove(test_file);
  }

  // 创建使用no策略的AOF对象
  Aof aof(test_file, AofSyncStrategy::NO);

  // 写入命令
  for (int i = 1; i <= 10; i++) {
    auto cmd = create_test_command("key" + std::to_string(i),
                                   "value" + std::to_string(i));
    aof.append(cmd);
  }

  // 文件应该已创建，但由于操作系统的缓冲机制，可能未完全写入磁盘
  TEST_ASSERT(std::filesystem::exists(test_file), "AOF文件未创建");

  // 手动触发fsync，确保数据写入磁盘
  aof.fsync_async();

  // 加载命令并验证
  auto commands = aof.load_commands();
  TEST_ASSERT(commands.size() == 10, "加载的命令数量不正确");

  // 清理测试文件
  std::filesystem::remove(test_file);
  return true;
}

// 模拟结合定时器和AOF的everysec策略
bool test_aof_with_timer_simulation() {
  std::cout << "模拟定时器触发AOF的everysec策略..." << std::endl;

  // 准备测试文件
  std::string test_file = "test_with_timer.aof";
  if (std::filesystem::exists(test_file)) {
    std::filesystem::remove(test_file);
  }

  // 创建AOF对象
  Aof aof(test_file, AofSyncStrategy::EVERYSEC);

  // 创建定时器队列
  TimerQueue timer_queue;

  // 添加AOF刷盘定时器，每100ms触发一次(为了加速测试)
  timer_queue.add_timer(
      std::chrono::milliseconds(100), [&aof]() { aof.fsync_async(); }, true,
      std::chrono::milliseconds(100));

  // 模拟执行一系列写操作
  for (int i = 1; i <= 5; i++) {
    auto cmd = create_test_command("key" + std::to_string(i),
                                   "value" + std::to_string(i));
    aof.append(cmd);

    // 模拟间隔30ms的操作
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
  }

  // 等待超过一个定时器周期，确保至少触发一次
  std::this_thread::sleep_for(std::chrono::milliseconds(120));

  // 模拟定时器触发
  timer_queue.process_timer_event();

  // 继续写入
  for (int i = 6; i <= 10; i++) {
    auto cmd = create_test_command("key" + std::to_string(i),
                                   "value" + std::to_string(i));
    aof.append(cmd);

    // 模拟间隔30ms的操作
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
  }

  // 再次等待并触发定时器
  std::this_thread::sleep_for(std::chrono::milliseconds(120));
  timer_queue.process_timer_event();

  // 加载命令并验证
  auto commands = aof.load_commands();
  TEST_ASSERT(commands.size() == 10,
              "加载的命令数量不正确，期望10个命令，实际有 " +
                  std::to_string(commands.size()) + " 个");

  // 清理测试文件
  std::filesystem::remove(test_file);
  return true;
}

// 测试AOF文件的加载和解析功能
bool test_aof_load_commands() {
  std::cout << "测试AOF文件加载功能..." << std::endl;

  // 准备测试文件
  std::string test_file = "test_load.aof";
  if (std::filesystem::exists(test_file)) {
    std::filesystem::remove(test_file);
  }

  // 手动创建AOF文件内容，包含一些RESP格式的命令
  {
    std::ofstream outfile(test_file);
    // SET key1 value1
    outfile << "*3\r\n$3\r\nSET\r\n$4\r\nkey1\r\n$6\r\nvalue1\r\n";
    // SET key2 value2
    outfile << "*3\r\n$3\r\nSET\r\n$4\r\nkey2\r\n$6\r\nvalue2\r\n";
    outfile.close();
  }

  // 创建AOF对象并加载命令
  Aof aof(test_file, AofSyncStrategy::ALWAYS);
  auto commands = aof.load_commands();

  // 验证加载了正确数量的命令
  TEST_ASSERT(commands.size() == 2, "加载的命令数量不正确");

  // 验证命令内容
  if (commands.size() >= 2) {
    auto &cmd1 = std::get<std::unique_ptr<resp::RespArray>>(commands[0]);
    auto &cmd2 = std::get<std::unique_ptr<resp::RespArray>>(commands[1]);

    TEST_ASSERT(cmd1->values.size() == 3, "第一个命令参数数量错误");
    TEST_ASSERT(cmd2->values.size() == 3, "第二个命令参数数量错误");

    auto &cmd1_name = std::get<resp::RespBulkString>(cmd1->values[0]);
    auto &cmd2_name = std::get<resp::RespBulkString>(cmd2->values[0]);

    TEST_ASSERT(cmd1_name.value && *cmd1_name.value == "SET",
                "第一个命令名错误");
    TEST_ASSERT(cmd2_name.value && *cmd2_name.value == "SET",
                "第二个命令名错误");
  }

  // 清理测试文件
  std::filesystem::remove(test_file);
  return true;
}

int main() {
  // 初始化日志
  Logger::instance().set_level(LogLevel::INFO);

  bool all_passed = true;
  std::vector<std::pair<std::string, std::function<bool()>>> tests = {
      {"Always同步策略测试", test_always_sync_strategy},
      {"Everysec同步策略测试", test_everysec_sync_strategy},
      {"No同步策略测试", test_no_sync_strategy},
      {"AOF结合定时器模拟测试", test_aof_with_timer_simulation},
      {"AOF文件加载测试", test_aof_load_commands},
  };

  int passed = 0;
  int failed = 0;

  std::cout << "开始执行AOF同步策略单元测试..." << std::endl;

  for (const auto &[name, test_func] : tests) {
    std::cout << "\n执行测试: " << name << std::endl;
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

  std::cout << "\n测试结果摘要:" << std::endl;
  std::cout << "总计: " << tests.size() << " 个测试" << std::endl;
  std::cout << "通过: " << passed << " 个测试" << std::endl;
  std::cout << "失败: " << failed << " 个测试" << std::endl;

  return all_passed ? 0 : 1;
}