module;

#include <atomic>
#include <cctype>
#include <chrono>
#include <format>
#include <memory>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

export module kv_server;

import resp;
import logger; // 导入日志模块
import aof;    // 导入 AOF 模块
import server_stat;
import timer;
import command;

export class KVServer {
public:
    KVServer() {
        LOG_INFO("KV存储服务已初始化");
        context_ = std::make_unique<KVServerContext>(db_, nullptr, stats_);
        command_factory_ = std::make_unique<KVCommandFactory>(*context_);
    }

    // 设置aof对象，更新上下文和命令工厂
    void set_aof(Aof *aof) {
        aof_ = aof;
        context_ = std::make_unique<KVServerContext>(db_, aof_, stats_);
        command_factory_ = std::make_unique<KVCommandFactory>(*context_);
    }

    // 设置定时器队列
    void set_timer_queue(TimerQueue *timer_queue) {
        timer_queue_ = timer_queue;
        // 创建定期清理任务
        setup_expire_cleanup_task();
    }

    // 暴露给外层网络库调用的静态方法
    static void increment_clients() { stats_.increment_clients(); }
    static void decrement_clients() { stats_.decrement_clients(); }

    // 主命令执行入口
    std::string execute_command(const resp::RespValue &command_variant, bool from_aof = false) {
        if (!from_aof) {
            stats_.increment_commands_processed();
        }

        // 创建命令
        auto command = command_factory_->create_command(command_variant, from_aof);

        // 执行命令
        std::string result = command->execute();

        // 处理复制
        if (command->should_replicate() && aof_) {
            aof_->append(command->get_original_command());
        }

        return result;
    }

    // 事务执行函数 - 处理一组事务命令
    std::string execute_transaction(const std::vector<resp::RespValue> &commands) {
        if (commands.empty()) {
            return resp::serialize_array(std::vector<resp::RespValue>{}); // 空事务返回空数组
        }
        LOG_INFO("执行事务，共 {} 条命令", commands.size());

        // 创建事务结果数组
        auto result_array = std::make_unique<resp::RespArray>();
        result_array->values.reserve(commands.size());

        // 遍历执行每一条命令，并收集结果
        for (const auto &command : commands) {
            // 执行命令并获取响应
            std::string response_str = execute_command(command, false);

            // 将命令的执行结果解析为 RespValue，然后加入结果数组
            std::string_view response_view = response_str;
            auto resp_result = resp::parse(response_view);

            if (resp_result) {
                // 解析成功，添加到结果数组
                result_array->values.push_back(std::move(*resp_result));
            } else {
                // 解析失败，添加一个错误信息
                result_array->values.push_back(resp::RespError{"ERR failed to parse command response"});
            }
        }

        // 将事务执行结果序列化为RESP数组
        resp::RespValue result_value = std::move(result_array);
        return resp::serialize(result_value);
    }

private:
    Storage db_;                                            // 数据库
    Aof *aof_ = nullptr;                                    // AOF对象
    TimerQueue *timer_queue_ = nullptr;                     // 定时器队列
    inline static ServerStat stats_;                        // 服务器统计信息
    std::mt19937 random_generator_{std::random_device{}()}; // 随机数生成器
    std::unique_ptr<KVServerContext> context_;              // KVServer上下文
    std::unique_ptr<CommandFactory> command_factory_;       // 命令工厂

    // 设置清理过期键的定时任务
    void setup_expire_cleanup_task();
    // 定期删除过期键
    void cleanup_expired_keys();
    // 检查一个键是否过期
    bool is_key_expired(const std::string &key, const KeyValue &kv);
    // 删除一个过期键
    bool delete_expired_key(const std::string &key);
};

// --- 实现 ---

// 设置定期清理过期键的任务
void KVServer::setup_expire_cleanup_task() {
    if (!timer_queue_) {
        LOG_WARN("无法设置过期键清理任务：定时器队列未设置");
        return;
    }

    // 每秒执行一次过期键清理
    const auto interval = std::chrono::milliseconds(1000);
    timer_queue_->add_timer(interval, [this]() { this->cleanup_expired_keys(); }, true, interval);
    LOG_INFO("已设置每秒过期键清理任务");
}

// 定期清理过期键实现
void KVServer::cleanup_expired_keys() {
    // 我们采用随机采样的方式，避免一次扫描所有键
    constexpr int SAMPLE_SIZE = 20; // 每次随机采样20个键
    constexpr double CONTINUE_THRESHOLD = 0.25; // 如果超过25%的键已过期，继续清理

    if (db_.empty())
        return;

    LOG_DEBUG("开始过期键清理任务");

    // 计算本次应该采样的键数量
    int sample_count = std::min(static_cast<int>(db_.size()), SAMPLE_SIZE);
    int expired_count = 0; // 初始化过期键计数

    // 随机选择键进行检查并且预先分配空间
    std::vector<std::string> keys_to_check;
    keys_to_check.reserve(sample_count);

    // 使用随机数生成器从数据库中选择键
    if (db_.size() <= SAMPLE_SIZE) {
        // 如果键的总数小于等于采样数，直接检查所有键
        for (const auto &[key, _] : db_) {
            keys_to_check.push_back(key);
        }
    } else {
        // 否则随机采样
        std::uniform_int_distribution<size_t> dist(0, db_.size() - 1);
        for (int i = 0; i < sample_count; ++i) {
            size_t random_idx = dist(random_generator_);
            auto it = db_.begin();
            std::advance(it, random_idx);
            keys_to_check.push_back(it->first);
        }
    }

    // 检查并清理过期键
    for (const auto &key : keys_to_check) {
        auto it = db_.find(key);
        if (it != db_.end() && is_key_expired(key, it->second)) {
            if (delete_expired_key(key)) {
                expired_count++;
            }
        }
    }

    LOG_DEBUG("过期键清理任务: 采样 {} 个键，删除 {} 个过期键", keys_to_check.size(), expired_count);

    // 如果过期键比例超过阈值，立即再次执行清理
    double expired_ratio = static_cast<double>(expired_count) / keys_to_check.size();
    if (expired_ratio >= CONTINUE_THRESHOLD) {
        LOG_INFO("过期键比例较高 ({:.1f}%)，安排立即再次执行清理任务", expired_ratio * 100);
        cleanup_expired_keys();
    }
}

// 检查键是否过期
bool KVServer::is_key_expired(const std::string &key, const KeyValue &kv) {
    if (!kv.expires_at.has_value()) {
        return false; // 没有设置过期时间
    }

    auto now = std::chrono::steady_clock::now();
    return now >= kv.expires_at.value();
}

// 删除一个过期键
bool KVServer::delete_expired_key(const std::string &key) {
    auto it = db_.find(key);
    if (it == db_.end()) {
        return false;
    }

    if (!is_key_expired(key, it->second)) {
        return false; // 键未过期
    }

    LOG_DEBUG("删除过期键: {}", key);
    db_.erase(it);
    return true;
}