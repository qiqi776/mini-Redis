module;
#include <chrono>
#include <condition_variable>
#include <format>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

export module logger;

// 定义日志级别
export enum class LogLevel { DEBUG, INFO, WARN, ERROR, FATAL };

// 异步日志记录器
export class Logger {
public:
    // 获取 Logger 的单例实例
    static Logger &instance() {
        static Logger logger;
        return logger;
    }

    // 设置日志文件名
    void set_logfile(const std::string &filename) {
        std::unique_lock<std::mutex> lock(mutex_);
        logfile_ = filename;
    }

    // 设置要记录的最低日志级别
    void set_level(LogLevel level) {
        std::unique_lock<std::mutex> lock(mutex_);
        level_ = level;
    }

    // 日志接口：使用模板和 std::format
    template <typename... Args>
    void log(LogLevel level, std::format_string<Args...> fmt, Args &&...args) {
        // 如果当前日志级别低于设置的级别，直接返回
        if (level < level_)
            return;

        // 将日志级别枚举转换为字符串
        const char *level_str;
        switch (level) {
        case LogLevel::DEBUG:
            level_str = "DEBUG";
            break;
        case LogLevel::INFO:
            level_str = "INFO";
            break;
        case LogLevel::WARN:
            level_str = "WARN";
            break;
        case LogLevel::ERROR:
            level_str = "ERROR";
            break;
        case LogLevel::FATAL:
            level_str = "FATAL";
            break;
        }

        // 使用 std::format 格式化日志条目
        std::string log_entry = std::format(fmt, std::forward<Args>(args)...);

        // 组合成最终的日志字符串，直接在 std::format 中处理时间
        auto now = std::chrono::system_clock::now();
        std::string final_log = std::format("[{:%Y-%m-%d %H:%M:%S}] [{}] {}", now,
                                            level_str, log_entry);

        // --- 生产者代码 ---
        {
            // 加锁，将日志消息放入队列
            std::unique_lock<std::mutex> lock(mutex_);
            queue_.push(final_log);
        }
        // 通知后台线程有新的日志需要写入
        cond_.notify_one();
    }

    // 析构函数
    ~Logger() {
        exit_ = true;
        cond_.notify_one(); // 唤醒可能在等待的后台线程
        if (writer_thread_.joinable()) {
            writer_thread_.join(); // 等待线程执行完毕
        }
    }

private:
    // 单例模式：构造函数、拷贝和赋值操作都是私有的
    Logger() : level_(LogLevel::INFO), exit_(false) {
        writer_thread_ = std::thread(&Logger::writer_thread, this);
    }

    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;

    // 后台写日志线程的工作函数
    void writer_thread() {
        std::ofstream file_stream;
        while (true) {
            std::unique_lock<std::mutex> lock(mutex_);
            // --- 消费者代码 ---
            // 等待条件满足：队列不为空或收到退出信号
            cond_.wait(lock, [this] { return !queue_.empty() || exit_; });

            // 如果收到退出信号且队列已空，则安全退出
            if (exit_ && queue_.empty()) {
                break;
            }

            // 第一次需要写入时，打开日志文件
            if (!logfile_.empty() && !file_stream.is_open()) {
                file_stream.open(logfile_, std::ios::app);
            }

            // 使用 "swap trick" 技巧，将主队列与临时队列交换
            std::queue<std::string> temp_queue;
            queue_.swap(temp_queue);
            lock.unlock(); // 关键：尽快解锁

            // 将临时队列中的所有日志写入文件或控制台
            while (!temp_queue.empty()) {
                const std::string &entry = temp_queue.front();
                if (file_stream.is_open()) {
                    file_stream << entry << std::endl;
                } else {
                    // 如果没有指定日志文件，就输出到标准输出
                    std::cout << entry << std::endl;
                }
                temp_queue.pop();
            }
        }
    }

    LogLevel level_;                // 当前日志级别
    std::string logfile_;           // 日志文件名
    std::queue<std::string> queue_; // 日志消息队列 (缓冲区)
    std::mutex mutex_;              // 保护队列的互斥锁
    std::condition_variable cond_; // 用于生产者-消费者模型的条件变量
    std::thread writer_thread_;    // 后台写入线程
    bool exit_;                    // 退出标志
};

// 提供简洁的日志宏，参考工业级项目的做法
// 这些宏可以大大简化日志调用的语法
export template <typename... Args>
inline void LOG_DEBUG(std::format_string<Args...> fmt, Args &&...args) {
    Logger::instance().log(LogLevel::DEBUG, fmt, std::forward<Args>(args)...);
}

export template <typename... Args>
inline void LOG_INFO(std::format_string<Args...> fmt, Args &&...args) {
    Logger::instance().log(LogLevel::INFO, fmt, std::forward<Args>(args)...);
}

export template <typename... Args>
inline void LOG_WARN(std::format_string<Args...> fmt, Args &&...args) {
    Logger::instance().log(LogLevel::WARN, fmt, std::forward<Args>(args)...);
}

export template <typename... Args>
inline void LOG_ERROR(std::format_string<Args...> fmt, Args &&...args) {
    Logger::instance().log(LogLevel::ERROR, fmt, std::forward<Args>(args)...);
}

export template <typename... Args>
inline void LOG_FATAL(std::format_string<Args...> fmt, Args &&...args) {
    Logger::instance().log(LogLevel::FATAL, fmt, std::forward<Args>(args)...);
}