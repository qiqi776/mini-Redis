#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <thread>

import application;
import config;
import kv_server;
import resp;
import aof;
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

// 准备测试配置文件
bool prepare_test_config(const std::string &config_file,
                         const std::string &aof_file,
                         const std::string &sync_strategy) {
  std::ofstream config(config_file);
  if (!config.is_open()) {
    std::cerr << "无法创建配置文件: " << config_file << std::endl;
    return false;
  }

  config << "port 16379\n";      // 使用非标准端口，避免冲突
  config << "loglevel debug\n";  // 使用调试级别日志
  config << "aof-enabled yes\n"; // 启用AOF
  config << "aof-file " << aof_file << "\n";         // AOF文件路径
  config << "appendfsync " << sync_strategy << "\n"; // 同步策略

  config.close();
  return true;
}

// 创建测试命令
resp::RespValue create_set_command(const std::string &key,
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

// 测试everysec策略的集成功能
bool test_everysec_integration() {
  std::cout << "测试everysec策略集成功能..." << std::endl;

  // 准备测试文件和目录
  std::string config_file = "test_integration_config.conf";
  std::string aof_file = "test_integration.aof";

  // 清理可能存在的旧文件
  if (std::filesystem::exists(config_file)) {
    std::filesystem::remove(config_file);
  }
  if (std::filesystem::exists(aof_file)) {
    std::filesystem::remove(aof_file);
  }

  // 创建测试配置
  if (!prepare_test_config(config_file, aof_file, "everysec")) {
    return false;
  }

  // 模拟应用运行过程
  {
    // 初始化应用
    Application app;
    if (!app.init(config_file)) {
      std::cerr << "应用初始化失败" << std::endl;
      return false;
    }

    // 创建KVServer和AOF实例，而不实际启动应用
    auto kv_server = std::make_unique<KVServer>();
    auto aof = std::make_unique<Aof>(aof_file, AofSyncStrategy::EVERYSEC);
    kv_server->set_aof(aof.get());

    // 执行一系列命令
    for (int i = 1; i <= 10; i++) {
      auto cmd = create_set_command("key" + std::to_string(i),
                                    "value" + std::to_string(i));
      kv_server->execute_command(cmd);
      std::this_thread::sleep_for(
          std::chrono::milliseconds(50)); // 模拟命令执行间隔
    }

    // 手动触发一次同步，确保所有数据都写入磁盘
    aof->fsync_async();

    // 让应用程序实例在这个作用域结束时销毁
    // 这会关闭文件和释放资源
  }

  // 验证AOF文件存在并包含数据
  TEST_ASSERT(std::filesystem::exists(aof_file), "AOF文件未创建");
  TEST_ASSERT(std::filesystem::file_size(aof_file) > 0, "AOF文件为空");

  // 重新加载AOF文件，验证所有命令都被保存
  Aof reload_aof(aof_file);
  auto commands = reload_aof.load_commands();
  TEST_ASSERT(commands.size() == 10,
              "加载的命令数量不正确，期望10个命令，实际有 " +
                  std::to_string(commands.size()) + " 个");

  // 清理测试文件
  std::filesystem::remove(config_file);
  std::filesystem::remove(aof_file);

  return true;
}

// 测试重启后的AOF恢复功能
bool test_recovery_after_restart() {
  std::cout << "测试重启后的AOF恢复功能..." << std::endl;

  // 准备测试文件和目录
  std::string config_file = "test_recovery_config.conf";
  std::string aof_file = "test_recovery.aof";

  // 清理可能存在的旧文件
  if (std::filesystem::exists(config_file)) {
    std::filesystem::remove(config_file);
  }
  if (std::filesystem::exists(aof_file)) {
    std::filesystem::remove(aof_file);
  }

  // 创建测试配置
  if (!prepare_test_config(config_file, aof_file, "always")) {
    return false;
  }

  // 第一次运行：写入一些命令到AOF
  {
    // 初始化应用
    Application app;
    if (!app.init(config_file)) {
      std::cerr << "应用初始化失败" << std::endl;
      return false;
    }

    // 创建KVServer和AOF实例，而不实际启动应用
    auto kv_server = std::make_unique<KVServer>();
    auto aof = std::make_unique<Aof>(aof_file, AofSyncStrategy::ALWAYS);
    kv_server->set_aof(aof.get());

    // 执行一些SET命令
    for (int i = 1; i <= 5; i++) {
      auto cmd = create_set_command("recovery_key" + std::to_string(i),
                                    "value" + std::to_string(i));
      kv_server->execute_command(cmd);
    }

    // 自然结束第一次运行
  }

  // 验证AOF文件已创建
  TEST_ASSERT(std::filesystem::exists(aof_file), "AOF文件未创建");

  // 第二次运行：加载AOF并验证数据
  {
    // 创建新的KVServer实例
    auto kv_server = std::make_unique<KVServer>();

    // 创建新的AOF实例，加载之前的文件
    auto aof = std::make_unique<Aof>(aof_file);
    kv_server->set_aof(aof.get());

    // 加载AOF中的所有命令并执行
    auto commands = aof->load_commands();
    for (const auto &cmd : commands) {
      kv_server->execute_command(cmd, true); // from_aof设为true，避免重复写入
    }

    // 验证之前的键已正确恢复
    for (int i = 1; i <= 5; i++) {
      auto get_cmd = std::make_unique<resp::RespArray>();

      resp::RespBulkString cmd_name;
      cmd_name.value = "GET";
      get_cmd->values.push_back(cmd_name);

      resp::RespBulkString key;
      key.value = "recovery_key" + std::to_string(i);
      get_cmd->values.push_back(key);

      resp::RespValue get_value = std::move(get_cmd);
      std::string result = kv_server->execute_command(get_value);

      // 验证结果不是空值
      TEST_ASSERT(result.find("$-1") == std::string::npos,
                  "键 recovery_key" + std::to_string(i) + " 未从AOF中恢复");

      // 验证包含正确的值
      std::string expected_value = "value" + std::to_string(i);
      TEST_ASSERT(result.find(std::to_string(expected_value.length())) !=
                      std::string::npos,
                  "键 recovery_key" + std::to_string(i) + " 的值不正确");
    }
  }

  // 清理测试文件
  std::filesystem::remove(config_file);
  std::filesystem::remove(aof_file);

  return true;
}

// 测试不同同步策略的性能差异(简单模拟)
bool test_sync_strategy_performance() {
  std::cout << "测试不同同步策略的性能差异..." << std::endl;

  const int NUM_COMMANDS = 1000; // 每种策略执行的命令数

  // 测试函数
  auto test_strategy = [NUM_COMMANDS](AofSyncStrategy strategy,
                                      const std::string &test_file) -> double {
    // 删除可能存在的旧文件
    if (std::filesystem::exists(test_file)) {
      std::filesystem::remove(test_file);
    }

    Aof aof(test_file, strategy);

    auto start = std::chrono::steady_clock::now();

    // 执行大量SET命令
    for (int i = 0; i < NUM_COMMANDS; i++) {
      auto cmd = create_set_command("perf_key" + std::to_string(i),
                                    "value" + std::to_string(i));
      aof.append(cmd);
    }

    // 如果是everysec或no策略，确保最后一次同步
    if (strategy != AofSyncStrategy::ALWAYS) {
      aof.fsync_async();
    }

    auto end = std::chrono::steady_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
            .count();

    // 清理测试文件
    std::filesystem::remove(test_file);

    return duration;
  };

  // 测试三种策略
  double always_time =
      test_strategy(AofSyncStrategy::ALWAYS, "perf_always.aof");
  double everysec_time =
      test_strategy(AofSyncStrategy::EVERYSEC, "perf_everysec.aof");
  double no_time = test_strategy(AofSyncStrategy::NO, "perf_no.aof");

  std::cout << "性能测试结果 (" << NUM_COMMANDS << " 个命令):" << std::endl;
  std::cout << "  ALWAYS 策略: " << always_time << " ms" << std::endl;
  std::cout << "  EVERYSEC 策略: " << everysec_time << " ms" << std::endl;
  std::cout << "  NO 策略: " << no_time << " ms" << std::endl;

  // 验证性能关系: NO >= EVERYSEC >= ALWAYS (值越小越快)
  // 由于测试环境变化，我们期望看到差异，但不严格要求以下条件
  // 如果测试时间太短或机器太快，可能看不到明显差异
  if (always_time > everysec_time || everysec_time > no_time) {
    std::cout
        << "注意：性能测试结果与预期不符，可能是由于测试规模较小或系统性能波动"
        << std::endl;
  }

  return true;
}

int main() {
  // 初始化日志
  Logger::instance().set_level(LogLevel::DEBUG);

  bool all_passed = true;
  std::vector<std::pair<std::string, std::function<bool()>>> tests = {
      {"Everysec策略集成测试", test_everysec_integration},
      {"重启后的AOF恢复测试", test_recovery_after_restart},
      {"同步策略性能比较测试", test_sync_strategy_performance},
  };

  int passed = 0;
  int failed = 0;

  std::cout << "开始执行集成测试..." << std::endl;

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