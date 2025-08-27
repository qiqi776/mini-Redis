module;

#include <atomic>
#include <chrono>
#include <format>
#include <string>

export module server_stat;

// ServerStat 类用于跟踪和报告服务器的统计信息。
export class ServerStat {
public:
  // 构造函数，记录服务器启动时间。
  ServerStat() : start_time_(std::chrono::steady_clock::now()) {}

  // 增加当前连接的客户端数量。
  void increment_clients() { connected_clients_++; }
  // 减少当前连接的客户端数量。
  void decrement_clients() { connected_clients_--; }
  // 增加已处理的命令总数。
  void increment_commands_processed() { total_commands_processed_++; }
  // 增加键空间命中次数。
  void increment_keyspace_hits() { keyspace_hits_++; }
  // 增加键空间未命中次数。
  void increment_keyspace_misses() { keyspace_misses_++; }

  // 生成并返回格式化的服务器信息字符串，类似于 Redis 的 INFO 命令。
  // @param num_keys 数据库中的键总数。
  std::string get_info(size_t num_keys) const {
    auto now = std::chrono::steady_clock::now();
    auto uptime =
        std::chrono::duration_cast<std::chrono::seconds>(now - start_time_)
            .count();

    std::string info_str;
    // --- 服务器信息 ---
    info_str += "# Server\r\n";
    info_str += "version:0.1.0\r\n";
    info_str += std::format("uptime_in_seconds:{}\r\n", uptime);
    info_str += "\r\n";

    // --- 客户端信息 ---
    info_str += "# Clients\r\n";
    info_str +=
        std::format("connected_clients:{}\r\n", connected_clients_.load());
    info_str += "\r\n";

    // --- 统计数据 ---
    info_str += "# Stats\r\n";
    info_str += std::format("total_commands_processed:{}\r\n",
                            total_commands_processed_.load());
    info_str += std::format("keyspace_hits:{}\r\n", keyspace_hits_.load());
    info_str += std::format("keyspace_misses:{}\r\n", keyspace_misses_.load());
    info_str += "\r\n";

    // --- 键空间信息 ---
    info_str += "# Keyspace\r\n";
    info_str += std::format("db0:keys={},expires=0,avg_ttl=0\r\n", num_keys);

    return info_str;
  }

private:
  // 原子变量，用于线程安全地跟踪连接的客户端数量。
  std::atomic<int> connected_clients_{0};
  // 原子变量，用于线程安全地跟踪已处理的命令总数。
  std::atomic<long long> total_commands_processed_{0};
  // 原子变量，用于线程安全地跟踪键空间命中次数。
  std::atomic<long long> keyspace_hits_{0};
  // 原子变量，用于线程安全地跟踪键空间未命中次数。
  std::atomic<long long> keyspace_misses_{0};
  // 服务器启动时间点，用于计算运行时长。
  std::chrono::steady_clock::time_point start_time_;
};