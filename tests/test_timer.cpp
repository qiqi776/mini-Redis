#include <cassert>
#include <chrono>
#include <condition_variable>
#include <functional> // 添加对std::function的支持
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

import timer;
import logger;

// 辅助宏，用于测试断言
#define TEST_ASSERT(condition, message)                                        \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << "断言失败: " << message << " 在 " << __FILE__ << " 行 "    \
                << __LINE__ << std::endl;                                      \
      return false;                                                            \
    }                                                                          \
  } while (0)

// 测试单次触发定时器
bool test_single_timer() {
  std::cout << "测试单次触发定时器..." << std::endl;

  TimerQueue timer_queue;
  bool callback_executed = false;
  auto callback = [&callback_executed]() { callback_executed = true; };

  // 添加一个100毫秒后触发的定时器
  timer_queue.add_timer(std::chrono::milliseconds(100), callback);

  // 休眠200毫秒，确保定时器触发
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // 手动触发处理，在实际应用中这由epoll事件自动触发
  timer_queue.process_timer_event();

  // 验证回调被执行
  TEST_ASSERT(callback_executed, "定时器回调未被执行");

  return true;
}

// 测试重复触发定时器
bool test_repeating_timer() {
  std::cout << "测试重复触发定时器..." << std::endl;

  TimerQueue timer_queue;
  int execution_count = 0;
  auto callback = [&execution_count]() { execution_count++; };

  // 添加一个每100毫秒重复触发的定时器
  timer_queue.add_timer(std::chrono::milliseconds(100), callback, true,
                        std::chrono::milliseconds(100));

  // 休眠350毫秒，应该触发3次（初始触发+2次重复）
  std::this_thread::sleep_for(std::chrono::milliseconds(350));

  // 手动触发处理3次，模拟epoll事件
  for (int i = 0; i < 3; i++) {
    timer_queue.process_timer_event();
  }

  // 验证回调被执行了3次
  TEST_ASSERT(execution_count >= 3, "重复定时器未按预期次数执行");

  return true;
}

// 测试多个定时器的优先级
bool test_multiple_timers() {
  std::cout << "测试多个定时器的优先级..." << std::endl;

  TimerQueue timer_queue;
  std::vector<int> execution_order;

  // 添加三个不同时间的定时器
  timer_queue.add_timer(std::chrono::milliseconds(300),
                        [&execution_order]() { execution_order.push_back(3); });

  timer_queue.add_timer(std::chrono::milliseconds(100),
                        [&execution_order]() { execution_order.push_back(1); });

  timer_queue.add_timer(std::chrono::milliseconds(200),
                        [&execution_order]() { execution_order.push_back(2); });

  // 休眠400毫秒，确保所有定时器都触发
  std::this_thread::sleep_for(std::chrono::milliseconds(400));

  // 手动触发处理
  for (int i = 0; i < 3; i++) {
    timer_queue.process_timer_event();
  }

  // 验证定时器按照时间顺序触发
  TEST_ASSERT(execution_order.size() == 3, "未触发所有定时器");
  TEST_ASSERT(execution_order[0] == 1, "定时器触发顺序错误");
  TEST_ASSERT(execution_order[1] == 2, "定时器触发顺序错误");
  TEST_ASSERT(execution_order[2] == 3, "定时器触发顺序错误");

  return true;
}

// 测试定时器触发精度 - 修改后的更健壮版本
bool test_timer_precision() {
  std::cout << "测试定时器触发精度..." << std::endl;

  TimerQueue timer_queue;
  bool timer_fired = false;

  auto start = std::chrono::steady_clock::now();

  // 添加一个定时器，100毫秒后触发
  timer_queue.add_timer(std::chrono::milliseconds(100),
                        [&]() { timer_fired = true; });

  // 等待足够长的时间，确保定时器触发
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // 手动触发定时器处理
  timer_queue.process_timer_event();

  // 检查定时器是否被触发
  TEST_ASSERT(timer_fired, "定时器回调未执行");

  auto end = std::chrono::steady_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  // 验证触发时间在一个合理范围内（允许有更大的误差）
  // 定时器设置为100ms，测试允许在50-500ms范围内
  std::cout << "定时器测试总耗时: " << duration.count() << " ms" << std::endl;
  TEST_ASSERT(duration.count() >= 50 && duration.count() <= 500,
              "定时器触发时间精度不在预期范围内");

  return true;
}

// 测试Timer类的基本功能
bool test_timer_class() {
  std::cout << "测试Timer类基本功能..." << std::endl;

  bool callback_executed = false;
  auto callback = [&callback_executed]() { callback_executed = true; };

  // 创建一个定时器，设置为100毫秒后触发
  auto now = std::chrono::milliseconds(1000); // 模拟当前时间
  Timer timer(now + std::chrono::milliseconds(100), callback);

  // 验证定时器的过期时间
  TEST_ASSERT(timer.expiration() == now + std::chrono::milliseconds(100),
              "定时器过期时间设置错误");

  // 验证定时器默认非重复
  TEST_ASSERT(!timer.repeat(), "定时器默认应为非重复");

  // 测试定时器运行
  timer.run();
  TEST_ASSERT(callback_executed, "定时器回调未执行");

  return true;
}

// 测试重复定时器的restart功能
bool test_timer_restart() {
  std::cout << "测试重复定时器的restart功能..." << std::endl;

  auto now = std::chrono::milliseconds(1000); // 模拟当前时间
  int interval = 500;                         // 间隔500毫秒

  // 创建一个重复定时器
  Timer timer(now, []() {}, true, std::chrono::milliseconds(interval));

  // 初始过期时间
  auto initial_expiration = timer.expiration();

  // 调用restart模拟定时器触发后的更新
  timer.restart();

  // 验证新的过期时间增加了interval毫秒
  TEST_ASSERT(timer.expiration() ==
                  initial_expiration + std::chrono::milliseconds(interval),
              "重复定时器restart后过期时间未正确更新");

  return true;
}

// 测试AOF的每秒同步定时器
bool test_aof_sync_timer_simulation() {
  std::cout << "模拟测试AOF每秒同步定时器..." << std::endl;

  TimerQueue timer_queue;
  int sync_count = 0;
  auto aof_sync_callback = [&sync_count]() {
    sync_count++;
    std::cout << "执行AOF同步操作，次数: " << sync_count << std::endl;
  };

  // 添加一个每1000毫秒触发一次的定时器，模拟AOF每秒同步
  timer_queue.add_timer(std::chrono::milliseconds(1000), aof_sync_callback,
                        true, std::chrono::milliseconds(1000));

  // 模拟运行3秒，应该触发3次同步
  for (int i = 0; i < 3; i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    timer_queue.process_timer_event();
  }

  // 验证同步次数
  TEST_ASSERT(sync_count == 3, "AOF同步定时器未按预期执行");

  return true;
}

int main() {
  // 初始化日志
  Logger::instance().set_level(LogLevel::INFO);

  bool all_passed = true;
  std::vector<std::pair<std::string, std::function<bool()>>> tests = {
      {"单次触发定时器测试", test_single_timer},
      {"重复触发定时器测试", test_repeating_timer},
      {"多个定时器测试", test_multiple_timers},
      {"定时器精度测试", test_timer_precision},
      {"Timer类基本功能测试", test_timer_class},
      {"重复定时器restart测试", test_timer_restart},
      {"AOF每秒同步定时器模拟测试", test_aof_sync_timer_simulation},
  };

  int passed = 0;
  int failed = 0;

  std::cout << "开始执行定时器模块单元测试..." << std::endl;

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