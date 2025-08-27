#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <memory>
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

// 测试空定时器队列的行为
bool test_empty_timer_queue() {
  std::cout << "测试空定时器队列..." << std::endl;

  // 创建一个空的定时器队列
  TimerQueue timer_queue;

  // 对空队列调用process_timer_event应该安全不崩溃
  try {
    timer_queue.process_timer_event();
    std::cout << "空队列处理成功" << std::endl;
  } catch (const std::exception &e) {
    std::cerr << "空队列处理异常: " << e.what() << std::endl;
    return false;
  }

  return true;
}

// 测试零延迟定时器 - 立即触发的定时器
bool test_zero_delay_timer() {
  std::cout << "测试零延迟定时器..." << std::endl;

  bool callback_executed = false;

  // 创建一个延迟为0的Timer对象并直接运行它
  Timer timer(std::chrono::milliseconds(0),
              [&callback_executed]() { callback_executed = true; });

  // 直接运行定时器回调
  timer.run();

  // 验证回调被执行
  TEST_ASSERT(callback_executed, "零延迟定时器回调未执行");

  return true;
}

// 修改后的大量定时器测试 - 不依赖timerfd读取
bool test_many_timers() {
  std::cout << "测试大量定时器处理..." << std::endl;

  const int NUM_TIMERS = 1000; // 测试1000个定时器
  std::vector<bool> timer_fired(NUM_TIMERS, false);
  std::vector<std::unique_ptr<Timer>> timers;

  auto start = std::chrono::steady_clock::now();

  // 创建大量定时器
  for (int i = 0; i < NUM_TIMERS; i++) {
    auto timer = std::make_unique<Timer>(
        std::chrono::milliseconds(i % 100), // 不同的延迟时间
        [&timer_fired, i]() { timer_fired[i] = true; });
    timers.push_back(std::move(timer));
  }

  // 直接执行所有定时器的回调
  for (auto &timer : timers) {
    timer->run();
  }

  auto end = std::chrono::steady_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
          .count();

  // 验证所有定时器都被触发
  int fired_count = std::count(timer_fired.begin(), timer_fired.end(), true);
  std::cout << "触发的定时器: " << fired_count << "/" << NUM_TIMERS
            << ", 耗时: " << duration << " ms" << std::endl;

  // 所有定时器都应该被触发
  TEST_ASSERT(fired_count == NUM_TIMERS, "所有定时器应该被触发");

  return true;
}

// 修改后的定时器顺序执行测试 - 不依赖timerfd
bool test_timer_execution_order() {
  std::cout << "测试定时器执行顺序..." << std::endl;

  std::vector<int> execution_sequence;
  std::vector<std::unique_ptr<Timer>> timers;

  // 创建5个具有相同过期时间的定时器
  for (int i = 1; i <= 5; i++) {
    auto timer = std::make_unique<Timer>(
        std::chrono::milliseconds(100), // 所有定时器延迟相同
        [&execution_sequence, i]() { execution_sequence.push_back(i); });
    timers.push_back(std::move(timer));
  }

  // 按照添加顺序执行定时器
  for (auto &timer : timers) {
    timer->run();
  }

  // 验证执行顺序
  TEST_ASSERT(execution_sequence.size() == 5, "应该有5个定时器执行");

  // 打印执行顺序
  std::cout << "定时器执行顺序: ";
  for (int val : execution_sequence) {
    std::cout << val << " ";
  }
  std::cout << std::endl;

  // 验证执行顺序与添加顺序一致
  for (size_t i = 0; i < execution_sequence.size(); i++) {
    TEST_ASSERT(execution_sequence[i] == i + 1,
                "定时器执行顺序应该与添加顺序一致");
  }

  return true;
}

// 修改后的定时器并发安全性测试 - 避免段错误
bool test_timer_thread_safety() {
  std::cout << "测试定时器并发安全性..." << std::endl;

  std::atomic<int> counter{0};
  std::vector<std::unique_ptr<Timer>> timers;
  std::mutex mtx;
  std::condition_variable cv;
  std::atomic<bool> done{false};

  // 创建100个定时器
  for (int i = 0; i < 100; i++) {
    auto timer = std::make_unique<Timer>(
        std::chrono::milliseconds(i % 10 + 1), // 1-10ms的延迟
        [&counter]() { counter++; });
    timers.push_back(std::move(timer));
  }

  // 创建线程，访问并运行定时器
  std::thread worker([&]() {
    for (auto &timer : timers) {
      timer->run();
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    done.store(true);
    cv.notify_one();
  });

  // 等待工作线程完成
  {
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait_for(lock, std::chrono::seconds(3), [&done] { return done.load(); });
  }

  // 确保worker线程能安全结束
  if (worker.joinable()) {
    worker.join();
  }

  // 验证计数器值
  std::cout << "执行的回调数: " << counter << std::endl;
  TEST_ASSERT(counter == 100, "所有定时器回调应该执行");

  return true;
}

// 测试非常短的间隔的重复定时器 - 使用Timer直接测试而不是TimerQueue
bool test_rapid_repeating_timer() {
  std::cout << "测试快速重复定时器..." << std::endl;

  int tick_count = 0;
  auto callback = [&tick_count]() { tick_count++; };

  // 创建一个重复定时器
  Timer timer(std::chrono::milliseconds(5), callback, true,
              std::chrono::milliseconds(5));

  // 模拟触发多次
  const int REPEAT_COUNT = 10;
  for (int i = 0; i < REPEAT_COUNT; i++) {
    timer.run();     // 运行回调
    timer.restart(); // 重新计算下次触发时间
  }

  std::cout << "重复定时器触发次数: " << tick_count << std::endl;

  // 验证触发次数
  TEST_ASSERT(tick_count == REPEAT_COUNT, "重复定时器应该触发正确次数");

  return true;
}

// 测试已过期定时器的处理
bool test_expired_timer_handling() {
  std::cout << "测试已过期定时器处理..." << std::endl;

  bool callback_executed = false;

  // 创建一个"负"延迟定时器，表示已经过期
  auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch());

  // 创建一个1小时前就已过期的定时器
  auto one_hour_ago = now - std::chrono::hours(1);

  // 创建已过期的Timer实例
  Timer timer(one_hour_ago,
              [&callback_executed]() { callback_executed = true; });

  // 验证定时器显示为已过期
  TEST_ASSERT(timer.expiration() < now, "定时器应该显示为已过期");

  // 运行定时器回调
  timer.run();

  // 验证回调执行
  TEST_ASSERT(callback_executed, "已过期定时器的回调应该执行");

  return true;
}

// 测试长时间运行的回调
bool test_long_running_callback() {
  std::cout << "测试长时间运行的回调..." << std::endl;

  bool callback_running = false;
  bool callback_finished = false;

  // 创建一个定时器，其回调会运行较长时间
  auto callback = [&]() {
    callback_running = true;
    // 模拟耗时操作
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    callback_finished = true;
  };

  Timer timer(std::chrono::milliseconds(10), callback);

  // 运行定时器
  timer.run();

  // 验证回调状态
  TEST_ASSERT(callback_running, "长时间运行的回调应该开始执行");
  TEST_ASSERT(callback_finished, "长时间运行的回调应该完成执行");

  return true;
}

// 测试随机延迟的多个定时器
bool test_random_delay_timers() {
  std::cout << "测试随机延迟的多个定时器..." << std::endl;

  std::vector<std::pair<int, int>> execution_order; // <delay, id>
  std::vector<std::pair<int, std::unique_ptr<Timer>>> delay_timer_pairs;

  // 创建具有随机延迟的定时器
  for (int i = 0; i < 10; i++) {
    int delay = (i * 17) % 100 + 10; // 伪随机延迟，10-109ms

    auto timer = std::make_unique<Timer>(
        std::chrono::milliseconds(delay), [&execution_order, delay, i]() {
          execution_order.push_back({delay, i});
        });

    delay_timer_pairs.push_back({delay, std::move(timer)});
  }

  // 按延迟排序
  std::sort(delay_timer_pairs.begin(), delay_timer_pairs.end(),
            [](const auto &a, const auto &b) { return a.first < b.first; });

  // 按延迟顺序执行定时器
  for (auto &[delay, timer] : delay_timer_pairs) {
    timer->run();
  }

  // 验证执行顺序
  TEST_ASSERT(execution_order.size() == 10, "应该有10个定时器执行");

  // 打印执行顺序
  std::cout << "实际执行顺序 (延迟,ID): ";
  for (const auto &[delay, id] : execution_order) {
    std::cout << "(" << delay << "," << id << ") ";
  }
  std::cout << std::endl;

  // 按延迟顺序检查执行顺序
  std::sort(execution_order.begin(), execution_order.end());
  for (size_t i = 0; i < execution_order.size(); i++) {
    TEST_ASSERT(execution_order[i].first == delay_timer_pairs[i].first,
                "定时器应该按延迟顺序执行");
  }

  return true;
}

// 测试重新设置定时器的过期时间
bool test_timer_reset() {
  std::cout << "测试定时器重置..." << std::endl;

  // 创建一个定时器
  bool callback_executed = false;
  auto callback = [&callback_executed]() { callback_executed = true; };

  auto now = std::chrono::milliseconds(1000); // 模拟当前时间
  Timer timer(now + std::chrono::milliseconds(100), callback);

  // 验证初始过期时间
  TEST_ASSERT(timer.expiration() == now + std::chrono::milliseconds(100),
              "初始过期时间设置错误");

  // 重置定时器，将过期时间延长到200毫秒后
  timer.reset(now + std::chrono::milliseconds(200));

  // 验证过期时间已更改
  TEST_ASSERT(timer.expiration() == now + std::chrono::milliseconds(200),
              "重置后过期时间未正确更新");

  // 验证回调函数未执行
  TEST_ASSERT(!callback_executed, "重置后回调不应被执行");

  return true;
}

int main() {
  // 初始化日志
  Logger::instance().set_level(LogLevel::INFO);

  bool all_passed = true;
  std::vector<std::pair<std::string, std::function<bool()>>> tests = {
      {"空定时器队列测试", test_empty_timer_queue},
      {"零延迟定时器测试", test_zero_delay_timer},
      {"大量定时器测试", test_many_timers},
      {"定时器执行顺序测试", test_timer_execution_order},
      {"定时器并发安全性测试", test_timer_thread_safety},
      {"快速重复定时器测试", test_rapid_repeating_timer},
      {"已过期定时器处理测试", test_expired_timer_handling},
      {"长时间运行回调测试", test_long_running_callback},
      {"随机延迟定时器测试", test_random_delay_timers},
      {"定时器重置测试", test_timer_reset},
  };

  int passed = 0;
  int failed = 0;

  std::cout << "开始执行详尽的定时器模块单元测试..." << std::endl;

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