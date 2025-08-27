module;
#include <chrono>
#include <memory>
#include <string>

export module application;
import logger;
import config;
import epoll_server;
import kv_server;
import aof;
import timer;

export class Application {
public:
    Application() = default;
    ~Application() = default;

    bool init(const std::string &config_file);

    void run();

private:
    std::unique_ptr<EpollServer> server_;
    std::unique_ptr<KVServer> kv_server_;
    std::unique_ptr<Aof> aof_;
};

bool Application::init(const std::string &config_file) {
    // 加载配置文件
    if (!config_file.empty()) {
        if (!Config::instance().load(config_file)) {
            return false;
        }
    }

    // 配置日志系统
    std::string log_file = Config::instance().get_string("logfile", "");
    if (!log_file.empty()) {
        Logger::instance().set_logfile(log_file);
    }

    // 设置日志级别
    std::string log_level = Config::instance().get_string("loglevel", "info");
    if (log_level == "debug") {
        Logger::instance().set_level(LogLevel::DEBUG);
    } else if (log_level == "info") {
        Logger::instance().set_level(LogLevel::INFO);
    } else if (log_level == "warn") {
        Logger::instance().set_level(LogLevel::WARN);
    } else if (log_level == "error") {
        Logger::instance().set_level(LogLevel::ERROR);
    } else if (log_level == "fatal") {
        Logger::instance().set_level(LogLevel::FATAL);
    }

    // 创建 KVServer
    kv_server_ = std::make_unique<KVServer>();

    // 配置 AOF
    if (Config::instance().get_string("aof-enabled", "no") == "yes") {
        std::string aof_file = Config::instance().get_string("aof-file", "dump.aof");
        // 获取AOF同步策略
        std::string aof_fsync = Config::instance().get_string("appendfsync", "always");
        AofSyncStrategy sync_strategy;

        if (aof_fsync == "everysec") {
            sync_strategy = AofSyncStrategy::EVERYSEC;
            LOG_INFO("使用 everysec AOF同步策略");
        } else if (aof_fsync == "no") {
            sync_strategy = AofSyncStrategy::NO;
            LOG_INFO("使用 no AOF同步策略");
        } else {
            // 默认使用always策略
            sync_strategy = AofSyncStrategy::ALWAYS;
            LOG_INFO("使用 always AOF同步策略");
        }

        aof_ = std::make_unique<Aof>(aof_file, sync_strategy);
        kv_server_->set_aof(aof_.get());

        auto commands = aof_->load_commands();
        for (const auto &cmd : commands) {
            kv_server_->execute_command(cmd, true);
        }
    }

    // 获取服务器端口
    int port = Config::instance().get_int("port", 6379);

    LOG_INFO("应用程序初始化成功");
    LOG_INFO("服务器将在端口 {} 上启动", port);

    // 创建服务器实例
    server_ = std::make_unique<EpollServer>(port, *kv_server_);

    // 获取EpollServer中的定时器队列，并将其设置到KVServer
    // 这样KVServer就可以使用定时器来进行过期键的清理
    TimerQueue *timer_queue = server_->get_time_queue();
    if(timer_queue) {
        kv_server_->set_timer_queue(timer_queue);
        LOG_INFO("已将定时器队列设置到KVServer，启用键过期功能");
    }

    // 如果AOF使用everysec策略，设置每秒刷盘定时器
    if (aof_ && Config::instance().get_string("appendfsync", "always") == "everysec") {
        // 创建一个每秒触发一次的定时器，用于AOF刷盘
        server_->add_timer(std::chrono::milliseconds(1000), [this]() {
            if (this->aof_) {
                this->aof_->fsync_async();
            }
            },
            true,                           // 重复执行
            std::chrono::milliseconds(1000) // 每1000毫秒执行一次
        );
        LOG_INFO("已设置AOF每秒刷盘定时器");
    }
    return true;
}

void Application::run() {
    if (server_) {
        server_->start();
    } else {
        LOG_FATAL("服务器未正确初始化");
    }
}
