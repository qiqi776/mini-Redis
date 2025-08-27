module;

#include <fstream>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module aof;

import logger;
import resp;

// AOF同步策略枚举
export enum class AofSyncStrategy {
    ALWAYS,   // 每次写命令都同步到磁盘
    EVERYSEC, // 每秒批量同步一次
    NO        // 由操作系统决定何时同步
};

export class Aof {
public:
    // 实现AOF单例模式
    Aof(const Aof &) = delete;
    Aof &operator=(const Aof&) = delete;

    explicit Aof(std::string filename, AofSyncStrategy sync_strategy = AofSyncStrategy::ALWAYS);
    void fsync_async();                           // 异步刷盘
    void append(const resp::RespValue &command);  // 追加命令
    std::vector<resp::RespValue> load_commands(); // 加载命令

private:
    std::string filename_; // 文件名
    std::ofstream file_;   // 写入文件的文件流
    AofSyncStrategy sync_strategy_; // 同步策略

    std::mutex buffer_mutex_;       // 保护缓冲区的互斥锁
    bool has_pending_sync_ = false; // 是否有待同步的数据
};

Aof::Aof(std::string filename, AofSyncStrategy sync_strategy) // 传入需要写入的文件名和同步策略
        : filename_(std::move(filename)), sync_strategy_(sync_strategy) {
    // 以输出和追加模式打开文件并且检查文件是否成功打开
    file_.open(filename_, std::ios::out | std::ios::app);
    if(!file_.is_open()) {
        // 文件打开失败，记录日志并抛出异常
        LOG_FATAL("无法打开文件 : {}", filename_);
        throw std::runtime_error("无法打开 AOF 文件");
    }
    // 记录文件打开成功的日志
    LOG_INFO("AOF 文件已打开: {}, 同步策略: {}", filename_,
            sync_strategy_ == AofSyncStrategy::ALWAYS     ? "always"
            : sync_strategy_ == AofSyncStrategy::EVERYSEC ? "everysec"
                                                            : "no");
}

void Aof::append(const resp::RespValue &command) {
    if (!file_.is_open()) {
        LOG_ERROR("AOF 文件未打开，无法追加命令");
        return;
    }
    // 将命令转换为RESP格式字符串
    std::string serialized_command = resp::serialize(command);
    // 加锁保护共享资源
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    // 写入文件并且刷新
    file_ << serialized_command;
    // 根据同步策略决定是否立即刷盘
    if (sync_strategy_ == AofSyncStrategy::ALWAYS) {
        // 立即刷盘
        file_.flush();
    } else {
        // everysec和no策略下，标记有待同步数据
        has_pending_sync_ = true;
    }
}

// 异步刷盘操作，由定时器触发
void Aof::fsync_async() {
    // 加锁保护共享资源
    std::lock_guard<std::mutex> lock(buffer_mutex_);

    // 如果有待同步数据，则执行刷盘操作
    if (has_pending_sync_ && file_.is_open()) {
        file_.flush();
        has_pending_sync_ = false;
        LOG_DEBUG("AOF 异步刷盘完成");
    }
}

// 服务器启动时，加载AOF文件中的命令
std::vector<resp::RespValue> Aof::load_commands() {
    std::vector<resp::RespValue> commands; // 存储加载的命令
    std::ifstream infile(filename_); // 打开AOF文件
    if(!infile.is_open()) {
        // aof文件不存在或者无法打开是正常情况（例如首次启动）
        LOG_INFO("未找到或无法打开 AOF 文件 : {}。将以空状态启动 ", filename_);
        return commands; // 返回空命令列表
    }
    LOG_INFO("正在加载 AOF 文件 : {}", filename_);

    // 一次性读取整个文件内容到字符串中，提高效率
    std::string content((std::istreambuf_iterator<char>(infile)), std::istreambuf_iterator<char>());

    if(content.empty()) {
        LOG_INFO("AOF 文件为空");
        return commands;
    }

    // 使用string_view循环解析文件内容
    std::string_view view = content;
    while(!view.empty()) {
        // 调用RESP解析器
        auto result = resp::parse(view);
        if(result.has_value()) {
            // 解析成功->将命令存入列表
            commands.push_back(std::move(result.value()));
        } else {
            // 解析失败
            if(result.error() == resp::ParseError::Incomplete) {
                LOG_WARN("AOF 文件解析不完整"); // 可能是服务器崩溃导致
                break; // 结束解析
            } else {
                // 这是真正的文件损坏或解析器bug
                LOG_ERROR("AOF 文件解析失败,终止加载");
                throw std::runtime_error("解析 AOF 文件失败");
            }
        }
    }
    LOG_INFO("AOF 文件加载完成，命令数量: {}", commands.size());
    return commands;
}