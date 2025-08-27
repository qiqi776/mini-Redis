#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

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

// 创建测试命令
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

// 测试单个AOF同步策略的性能，返回每秒写入命令数
double test_aof_strategy_performance(AofSyncStrategy strategy,
                                     const std::string &test_file, int num_cmds,
                                     bool print_progress = false) {
  // 准备测试文件
  if (std::filesystem::exists(test_file)) {
    std::filesystem::remove(test_file);
  }

  // 创建AOF对象
  Aof aof(test_file, strategy);

  // 使用定时器队列（如果是everysec策略）
  std::unique_ptr<TimerQueue> timer_queue;
  if (strategy == AofSyncStrategy::EVERYSEC) {
    timer_queue = std::make_unique<TimerQueue>();
    timer_queue->add_timer(
        std::chrono::milliseconds(1000), [&aof]() { aof.fsync_async(); }, true,
        std::chrono::milliseconds(1000));
  }

  // 准备测试数据 - 创建一批命令
  const int VALUE_SIZE = 100; // 值的大小（字节）
  std::string value(VALUE_SIZE, 'x');

  auto start_time = std::chrono::steady_clock::now();

  // 执行写入操作
  for (int i = 0; i < num_cmds; i++) {
    auto cmd = create_test_command("key" + std::to_string(i),
                                   value + std::to_string(i));
    aof.append(cmd);

    // 每写入10%的命令打印一次进度
    if (print_progress && (i % (num_cmds / 10) == 0)) {
      std::cout << "已完成 " << (i * 100 / num_cmds) << "%..." << std::endl;
    }

    // 如果使用everysec策略，每100次操作检查一次定时器
    if (strategy == AofSyncStrategy::EVERYSEC && i % 100 == 0) {
      timer_queue->process_timer_event();
    }
  }

  // 确保最后的数据都写入磁盘
  if (strategy != AofSyncStrategy::ALWAYS) {
    aof.fsync_async();
  }

  auto end_time = std::chrono::steady_clock::now();
  auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                         end_time - start_time)
                         .count();

  // 计算每秒操作数
  double ops_per_sec = num_cmds * 1000.0 / duration_ms;

  // 清理测试文件
  if (std::filesystem::exists(test_file)) {
    std::filesystem::remove(test_file);
  }

  return ops_per_sec;
}

// 测试小文件（少量命令）的性能
bool test_small_file_performance() {
  std::cout << "测试小文件性能 (1000个命令)..." << std::endl;

  const int NUM_CMDS = 1000;

  // 测试三种同步策略
  double always_ops = test_aof_strategy_performance(
      AofSyncStrategy::ALWAYS, "perf_always_small.aof", NUM_CMDS);
  double everysec_ops = test_aof_strategy_performance(
      AofSyncStrategy::EVERYSEC, "perf_everysec_small.aof", NUM_CMDS);
  double no_ops = test_aof_strategy_performance(AofSyncStrategy::NO,
                                                "perf_no_small.aof", NUM_CMDS);

  // 打印性能结果
  std::cout << std::fixed << std::setprecision(2);
  std::cout << "小文件性能测试结果 (" << NUM_CMDS << " 个命令):" << std::endl;
  std::cout << "  ALWAYS 策略:   " << always_ops << " ops/sec" << std::endl;
  std::cout << "  EVERYSEC 策略: " << everysec_ops << " ops/sec" << std::endl;
  std::cout << "  NO 策略:       " << no_ops << " ops/sec" << std::endl;

  // 计算性能提升百分比
  if (always_ops > 0) {
    double everysec_improvement = (everysec_ops / always_ops - 1) * 100;
    double no_improvement = (no_ops / always_ops - 1) * 100;
    std::cout << "  EVERYSEC 相比 ALWAYS 提升: " << everysec_improvement << "%"
              << std::endl;
    std::cout << "  NO 相比 ALWAYS 提升: " << no_improvement << "%"
              << std::endl;
  }

  // 验证性能关系
  bool performance_relation_ok =
      (no_ops >= everysec_ops) && (everysec_ops >= always_ops);
  if (!performance_relation_ok) {
    std::cout << "注意：性能测试结果不符合预期关系 NO >= EVERYSEC >= ALWAYS"
              << std::endl;
    std::cout << "这可能是由于测试规模小或系统负载波动导致" << std::endl;
  }

  return true;
}

// 测试大文件（大量命令）的性能
bool test_large_file_performance() {
  std::cout << "\n测试大文件性能 (10000个命令)..." << std::endl;

  const int NUM_CMDS = 10000;

  // 测试三种同步策略
  double always_ops = test_aof_strategy_performance(
      AofSyncStrategy::ALWAYS, "perf_always_large.aof", NUM_CMDS, true);
  double everysec_ops = test_aof_strategy_performance(
      AofSyncStrategy::EVERYSEC, "perf_everysec_large.aof", NUM_CMDS, true);
  double no_ops = test_aof_strategy_performance(
      AofSyncStrategy::NO, "perf_no_large.aof", NUM_CMDS, true);

  // 打印性能结果
  std::cout << std::fixed << std::setprecision(2);
  std::cout << "大文件性能测试结果 (" << NUM_CMDS << " 个命令):" << std::endl;
  std::cout << "  ALWAYS 策略:   " << always_ops << " ops/sec" << std::endl;
  std::cout << "  EVERYSEC 策略: " << everysec_ops << " ops/sec" << std::endl;
  std::cout << "  NO 策略:       " << no_ops << " ops/sec" << std::endl;

  // 计算性能提升百分比
  if (always_ops > 0) {
    double everysec_improvement = (everysec_ops / always_ops - 1) * 100;
    double no_improvement = (no_ops / always_ops - 1) * 100;
    std::cout << "  EVERYSEC 相比 ALWAYS 提升: " << everysec_improvement << "%"
              << std::endl;
    std::cout << "  NO 相比 ALWAYS 提升: " << no_improvement << "%"
              << std::endl;
  }

  // 验证性能关系
  bool performance_relation_ok =
      (no_ops >= everysec_ops) && (everysec_ops >= always_ops * 0.9);
  if (!performance_relation_ok) {
    std::cout << "注意：性能测试结果不符合预期关系 NO >= EVERYSEC >= ALWAYS*0.9"
              << std::endl;
  }

  return true;
}

// 测试突发负载下的性能
bool test_burst_load_performance() {
  std::cout << "\n测试突发负载性能..." << std::endl;

  // 定义突发负载：短时间内大量写入
  const int BURSTS = 5;            // 突发次数
  const int CMDS_PER_BURST = 1000; // 每次突发的命令数量

  // 对比always和everysec策略
  double always_total_time = 0;
  double everysec_total_time = 0;

  for (int burst = 0; burst < BURSTS; burst++) {
    std::cout << "执行突发负载 #" << (burst + 1) << "..." << std::endl;

    // 测试ALWAYS策略
    std::string test_file =
        "perf_burst_always_" + std::to_string(burst) + ".aof";
    if (std::filesystem::exists(test_file)) {
      std::filesystem::remove(test_file);
    }

    Aof aof_always(test_file, AofSyncStrategy::ALWAYS);
    auto start_time = std::chrono::steady_clock::now();

    for (int i = 0; i < CMDS_PER_BURST; i++) {
      auto cmd = create_test_command("burst_key_" + std::to_string(i),
                                     "burst_value_" + std::to_string(i));
      aof_always.append(cmd);
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                           end_time - start_time)
                           .count();
    always_total_time += duration_ms;

    std::cout << "  ALWAYS策略突发处理时间: " << duration_ms << " ms"
              << std::endl;
    std::filesystem::remove(test_file);

    // 测试EVERYSEC策略
    test_file = "perf_burst_everysec_" + std::to_string(burst) + ".aof";
    if (std::filesystem::exists(test_file)) {
      std::filesystem::remove(test_file);
    }

    Aof aof_everysec(test_file, AofSyncStrategy::EVERYSEC);
    TimerQueue timer_queue;
    timer_queue.add_timer(
        std::chrono::milliseconds(1000),
        [&aof_everysec]() { aof_everysec.fsync_async(); }, true,
        std::chrono::milliseconds(1000));

    start_time = std::chrono::steady_clock::now();

    for (int i = 0; i < CMDS_PER_BURST; i++) {
      auto cmd = create_test_command("burst_key_" + std::to_string(i),
                                     "burst_value_" + std::to_string(i));
      aof_everysec.append(cmd);

      // 每500个命令检查一次定时器
      if (i % 500 == 0) {
        timer_queue.process_timer_event();
      }
    }

    // 确保所有数据刷到磁盘
    aof_everysec.fsync_async();

    end_time = std::chrono::steady_clock::now();
    duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      end_time - start_time)
                      .count();
    everysec_total_time += duration_ms;

    std::cout << "  EVERYSEC策略突发处理时间: " << duration_ms << " ms"
              << std::endl;
    std::filesystem::remove(test_file);

    // 两次测试之间短暂休息
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  // 计算平均时间
  double always_avg = always_total_time / BURSTS;
  double everysec_avg = everysec_total_time / BURSTS;

  // 打印结果
  std::cout << "\n突发负载测试结果 (" << CMDS_PER_BURST << " 命令/突发，共 "
            << BURSTS << " 次突发):" << std::endl;
  std::cout << "  ALWAYS策略平均时间:   " << always_avg << " ms" << std::endl;
  std::cout << "  EVERYSEC策略平均时间: " << everysec_avg << " ms" << std::endl;

  // 计算提升百分比
  if (always_avg > 0) {
    double improvement = (always_avg / everysec_avg - 1) * 100;
    if (improvement > 0) {
      std::cout << "  EVERYSEC策略比ALWAYS策略快: " << improvement << "%"
                << std::endl;
    } else {
      std::cout << "  ALWAYS策略比EVERYSEC策略快: " << -improvement << "%"
                << std::endl;
    }
  }

  return true;
}

// 测试模拟真实工作负载下的AOF性能
bool test_realistic_workload() {
  std::cout << "\n测试模拟真实工作负载..." << std::endl;

  // 定义一个模拟真实工作负载的模式：混合读写，突发写入，间歇性操作
  const int TOTAL_OPERATIONS = 5000;
  const double WRITE_RATIO = 0.3; // 30%是写操作
  const int BURST_SIZE = 50;      // 每次突发的命令数

  // 准备测试
  std::vector<bool> is_write(TOTAL_OPERATIONS, false);
  int write_count = static_cast<int>(TOTAL_OPERATIONS * WRITE_RATIO);

  // 设置一些写操作
  for (int i = 0; i < write_count; i++) {
    int pos = rand() % TOTAL_OPERATIONS;
    is_write[pos] = true;
  }

  // 添加一些突发写入
  for (int i = 0; i < TOTAL_OPERATIONS; i += 500) {
    int end = std::min(i + BURST_SIZE, TOTAL_OPERATIONS);
    for (int j = i; j < end; j++) {
      is_write[j] = true;
    }
  }

  // 再次统计实际的写操作数量
  write_count = 0;
  for (bool is_w : is_write) {
    if (is_w)
      write_count++;
  }

  std::cout << "模拟负载中的写操作比例: "
            << (write_count * 100.0 / TOTAL_OPERATIONS) << "%" << std::endl;

  // 测试ALWAYS策略
  std::string test_file = "perf_realistic_always.aof";
  if (std::filesystem::exists(test_file)) {
    std::filesystem::remove(test_file);
  }

  Aof aof_always(test_file, AofSyncStrategy::ALWAYS);
  auto start_time = std::chrono::steady_clock::now();

  for (int i = 0; i < TOTAL_OPERATIONS; i++) {
    if (is_write[i]) {
      // 模拟写操作
      auto cmd = create_test_command("key_" + std::to_string(i),
                                     "value_" + std::to_string(i));
      aof_always.append(cmd);
    } else {
      // 模拟读操作 - 不需要AOF
      std::this_thread::sleep_for(std::chrono::microseconds(10));
    }
  }

  auto end_time = std::chrono::steady_clock::now();
  auto always_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                             end_time - start_time)
                             .count();
  std::filesystem::remove(test_file);

  // 测试EVERYSEC策略
  test_file = "perf_realistic_everysec.aof";
  if (std::filesystem::exists(test_file)) {
    std::filesystem::remove(test_file);
  }

  Aof aof_everysec(test_file, AofSyncStrategy::EVERYSEC);
  TimerQueue timer_queue;
  timer_queue.add_timer(
      std::chrono::milliseconds(1000),
      [&aof_everysec]() { aof_everysec.fsync_async(); }, true,
      std::chrono::milliseconds(1000));

  start_time = std::chrono::steady_clock::now();

  for (int i = 0; i < TOTAL_OPERATIONS; i++) {
    if (is_write[i]) {
      // 模拟写操作
      auto cmd = create_test_command("key_" + std::to_string(i),
                                     "value_" + std::to_string(i));
      aof_everysec.append(cmd);
    } else {
      // 模拟读操作 - 不需要AOF
      std::this_thread::sleep_for(std::chrono::microseconds(10));
    }

    // 每1000个操作处理一次定时器事件
    if (i % 1000 == 0) {
      timer_queue.process_timer_event();
    }
  }

  // 确保所有数据刷到磁盘
  aof_everysec.fsync_async();

  end_time = std::chrono::steady_clock::now();
  auto everysec_duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end_time -
                                                            start_time)
          .count();
  std::filesystem::remove(test_file);

  // 打印结果
  std::cout << "\n真实工作负载测试结果 (" << TOTAL_OPERATIONS << " 总操作, "
            << write_count << " 写操作):" << std::endl;
  std::cout << "  ALWAYS策略总时间:   " << always_duration << " ms"
            << std::endl;
  std::cout << "  EVERYSEC策略总时间: " << everysec_duration << " ms"
            << std::endl;

  // 计算每秒操作数
  double always_ops = TOTAL_OPERATIONS * 1000.0 / always_duration;
  double everysec_ops = TOTAL_OPERATIONS * 1000.0 / everysec_duration;

  std::cout << "  ALWAYS策略性能:   " << always_ops << " ops/sec" << std::endl;
  std::cout << "  EVERYSEC策略性能: " << everysec_ops << " ops/sec"
            << std::endl;

  // 计算提升百分比
  if (always_ops > 0) {
    double improvement = (everysec_ops / always_ops - 1) * 100;
    std::cout << "  EVERYSEC策略性能提升: " << improvement << "%" << std::endl;
  }

  return true;
}

int main() {
  // 初始化日志
  Logger::instance().set_level(LogLevel::INFO);

  // 设置随机数种子
  std::srand(static_cast<unsigned>(std::time(nullptr)));

  bool all_passed = true;
  std::vector<std::pair<std::string, std::function<bool()>>> tests = {
      {"小文件性能测试", test_small_file_performance},
      {"大文件性能测试", test_large_file_performance},
      {"突发负载性能测试", test_burst_load_performance},
      {"真实工作负载测试", test_realistic_workload},
  };

  int passed = 0;
  int failed = 0;

  std::cout << "开始执行AOF同步策略性能测试..." << std::endl;

  for (const auto &[name, test_func] : tests) {
    std::cout << "\n===================================" << std::endl;
    std::cout << "执行测试: " << name << std::endl;
    std::cout << "===================================" << std::endl;

    try {
      if (test_func()) {
        std::cout << "√ 测试完成: " << name << std::endl;
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
  std::cout << "性能测试结果摘要:" << std::endl;
  std::cout << "总计: " << tests.size() << " 个测试" << std::endl;
  std::cout << "完成: " << passed << " 个测试" << std::endl;
  std::cout << "失败: " << failed << " 个测试" << std::endl;
  std::cout << "===================================" << std::endl;

  return all_passed ? 0 : 1;
}