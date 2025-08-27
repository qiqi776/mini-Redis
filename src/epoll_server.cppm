module;
//包含了所有网络编程、epoll、文件控制等所需的Linux/POsIX系统头文件
#include <arpa/inet.h>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <expected>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <netinet/in.h>
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_map>
#include <variant>
#include <vector>

export module epoll_server;

import kv_server;
import resp;
import buffer;
import logger;
import timer;

const int MAX_EVENTS = 1024; // 最大事件数量
const int BUFFER_SIZE = 1024; // 缓冲区大小

// 连接状态，用于表示客户端当前是否在事务中
enum class ConnectionState {
    Normal,       // 正常状态
    InTransaction // 事务中
};

// 定义TCP连接类，封装客户端连接的状态和数据
struct TcpConnection {
    Buffer buffer;                                   // 客户端读写缓冲区
    ConnectionState state = ConnectionState::Normal; // 连接状态
    std::vector<resp::RespValue> transaction_queue;  // 事务命令队列
};

export class EpollServer {
public:
    EpollServer() = delete;
    explicit EpollServer(int port, KVServer &kv_server) : port_(port), kv_server_(kv_server) {}
    ~EpollServer();
    bool init(int port);
    void run();
    void start() {
        if (!initialized_) {
            if (!init(port_)) {
                LOG_FATAL("服务器初始化失败");
                return;
            }
        }
        run();
    }

    // 添加定时器，返回定时器指针
    Timer * add_timer(std::chrono::milliseconds when, TimerCallback cb, bool repeat = false,
                     std::chrono::milliseconds interval = std::chrono::milliseconds(0));
    // 获取定时器队列指针，供外部使用
    TimerQueue * get_time_queue() { return timer_queue_.get(); }
private:
    bool set_non_blocking(int fd); // 设置文件描述符为非阻塞
    void handle_new_connection(); // 处理新连接
    void handle_client_data(int clients_fd); // 处理客户端数据
    void close_client_connection(int client_fd); // 关闭客户端连接
    void handle_timer_event(); // 处理定时器事件

    int listen_fd_ = -1; // 服务器监听socket文件描述符
    int epoll_fd_ = -1; // epoll实例的文件描述符
    int port_ = 6379;          // 服务器端口
    bool initialized_ = false; // 是否已初始化

    std::unique_ptr<TimerQueue> timer_queue_; // 定时器队列
    std::unordered_map<int, TcpConnection> connections_; // 存储每个客户端的连接信息
    KVServer &kv_server_; // 共享的KVServer实例
};

EpollServer::~EpollServer() {
    if(listen_fd_ != -1) {
        LOG_DEBUG("关闭监听套接字 {}", listen_fd_);
        close(listen_fd_);
    }
    if(epoll_fd_ != -1) {
        LOG_DEBUG("关闭epoll实例 {}", epoll_fd_);
        close(epoll_fd_);
    }
}
// 初始化服务器
bool EpollServer::init(int port) {
    port_ = port; // 保存端口号
    // 创建一个tcp socket
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ == -1) {
        LOG_ERROR("创建socket失败: {}", strerror(errno));
        return false;
    }
    // 设置SO_REUSEADDR选项，允许地址重用
    int opt = 1;
    if(setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        LOG_ERROR("设置socket选项失败: {}", strerror(errno));
        return false;
    }

    // 准备服务器地址结构体
    sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr)); // 先清零
    server_addr.sin_family = AF_INET;             // IPv4
    server_addr.sin_addr.s_addr = INADDR_ANY;     // 监听所有可用的接口
    server_addr.sin_port = htons(port);           // 端口号

    // 绑定socket
    if (bind(listen_fd_, (sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        LOG_ERROR("绑定端口失败: {}", strerror(errno));
        return false;
    }

    // 开始监听
    if (listen(listen_fd_, SOMAXCONN) == -1) {
        LOG_ERROR("监听端口失败: {}", strerror(errno));
        return false;
    }

    // 将监听的socket设置为非阻塞
    if (!set_non_blocking(listen_fd_)) {
        return false;
    }

    // 创建epoll实例
    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ == -1) {
        LOG_ERROR("创建epoll实例失败: {}", strerror(errno));
        return false;
    }

    // 将监听socket添加到epoll中
    epoll_event event;
    event.events = EPOLLIN | EPOLLET;
    event.data.fd = listen_fd_;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listen_fd_, &event) == -1) {
        LOG_ERROR("将监听socket加入epoll失败: {}", strerror(errno));
        return false;
    }

    // 创建定时器队列
    timer_queue_ = std::make_unique<TimerQueue>();
    // 将定时器的文件描述符添加到 epoll 监控列表
    event.events = EPOLLIN; // 对于定时器事件，使用电平触发更安全
    event.data.fd = timer_queue_->timer_fd();
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, timer_queue_->timer_fd(), &event) == -1) {
        LOG_ERROR("将定时器文件描述符加入epoll失败: {}", strerror(errno));
        return false;
    }
    initialized_ = true;
    LOG_INFO("并发K/V服务器启动成功，监听端口：{}", port_);
    return true;
}
// 运行服务器
void EpollServer::run() {
    std::vector<epoll_event> events(MAX_EVENTS); // 用于接收就绪事件
    LOG_INFO("服务器开始运行");
    while(true){
        // 阻塞程序，直到有事件发生或者超时,n为就绪事件的数量
        int n = epoll_wait(epoll_fd_, events.data(), MAX_EVENTS, -1);
        if (n == -1) {
            if(errno == EINTR) continue; // 若是被信号中断,继续循环
            LOG_ERROR("epoll_wait错误: {}", strerror(errno));
            break;
        }
        // 遍历所有就绪事件
        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;
            // 判断就绪事件是否发生在监听socket上
            if (fd == listen_fd_) {
                handle_new_connection(); // 是则说明有新连接请求
            } else if (fd == timer_queue_->timer_fd()) {
                handle_timer_event();
            } else {
                handle_client_data(fd);//不是则说明已连接的客户端发来了数据
            }
        }
    }
    
}

// 处理定时器事件
void EpollServer::handle_timer_event() {
    if (timer_queue_) {
        timer_queue_->process_timer_event();
    }
}

// 对外提供的添加定时器接口
Timer *EpollServer::add_timer(std::chrono::milliseconds when, TimerCallback cb,
                              bool repeat, std::chrono::milliseconds interval) {
    if (timer_queue_) {
        return timer_queue_->add_timer(when, std::move(cb), repeat, interval);
    }
    return nullptr;
}

// 设置文件描述符为非阻塞
bool EpollServer::set_non_blocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0); // 获得fd当前的状态标志
    if (flags == -1) {
        LOG_ERROR("设置非阻塞模式失败(F_GETFL): {}", strerror(errno));
        return false;
    }
    // 设置fd为非阻塞
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        LOG_ERROR("设置非阻塞模式失败(F_SETFL): {}", strerror(errno));
        return false;
    }
    return true;
}
// 处理新连接
void EpollServer::handle_new_connection() {
    // ET 模式下，多个连接到来时，epoll 可能只通知一次。
    // 所以必须用循环把所有等待的连接都 accept 掉。
    while (true) {
        // 接受新连接
        sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int conn_fd = accept(listen_fd_, (sockaddr *)&client_addr, &client_len);
        if (conn_fd == -1) {
            // 如果 errno 是 EAGAIN 或 EWOULDBLOCK，说明所有等待的连接都已处理完毕
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            // 其他错误则打印并退出
            LOG_ERROR("接受连接失败: {}", strerror(errno));
            break;
        }

        // 将新连接也设置为非阻塞模式
        set_non_blocking(conn_fd);

        // 准备新连接的 epoll 事件
        epoll_event event;
        event.events = EPOLLIN | EPOLLET; // 同样关心可读事件和使用 ET 模式
        event.data.fd = conn_fd;

        // 获取客户端IP地址
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
        int client_port = ntohs(client_addr.sin_port);

        // 将新连接的文件描述符添加到 epoll 的监控中
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, conn_fd, &event) == -1) {
            LOG_ERROR("将客户端 {} 加入epoll失败: {}", conn_fd, strerror(errno));
            close(conn_fd); // 添加失败，关闭这个连接
        } else {
            LOG_INFO("新客户端连接: #{} 来自 {}:{}", conn_fd, client_ip, client_port);
            // 为这个新客户端在 map 中创建一个专属的 Buffer 对象
            connections_[conn_fd];
            kv_server_.increment_clients();
        }
    }
}
// 关闭客户端连接
void EpollServer::close_client_connection(int client_fd) {
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, client_fd, nullptr);
    close(client_fd);
    connections_.erase(client_fd);
    kv_server_.decrement_clients();
}

// 处理客户端数据
void EpollServer::handle_client_data(int client_fd) {
    // 找到客户端的缓冲区
    auto it = connections_.find(client_fd);
    if (it == connections_.end()) {
        LOG_WARN("客户端 #{} 未找到对应的缓冲区", client_fd);
        return;
    }
    TcpConnection &conn = it->second; // 直接获取连接对象

    int saved_errno = 0;
    ssize_t n = conn.buffer.read_fd(client_fd, &saved_errno);

    if (n == 0) {
        // 客户端关闭连接
        LOG_INFO("客户端 #{} 断开连接", client_fd);
        close_client_connection(client_fd);
        return;
    }
    if (n < 0) {
        // 读取出错
        if (saved_errno != EAGAIN && saved_errno != EWOULDBLOCK) {
            LOG_ERROR("读取客户端 #{} 数据失败: {}", client_fd, strerror(saved_errno));
            close_client_connection(client_fd);
        }
        return;
    }
    LOG_DEBUG("从客户端 #{} 读取了 {} 字节数据", client_fd, n);
    // 循环地从缓冲区中解析完整的RESP消息
    while (conn.buffer.readable_bytes() > 0) {
        // 创建一个临时的 string_view 用于解析，因为它会被 resp::parse 修改
        auto readable_view = conn.buffer.readable_view();
        auto result = resp::parse(readable_view);

        if (result.has_value()) {
            // 解析成功，从原始缓冲区中“消费”掉已处理的数据
            // 注意：这里不再是昂贵的 erase，而是简单的索引移动
            conn.buffer.retrieve(conn.buffer.readable_view().size() - readable_view.size());
            // 记录命令执行日志
            // 修正：检查是否为数组类型，然后获取第一个元素
            if (std::holds_alternative<std::unique_ptr<resp::RespArray>>(result.value())) {
                auto &arr = std::get<std::unique_ptr<resp::RespArray>>(result.value());
                if (!arr->values.empty() && std::holds_alternative<resp::RespBulkString>(arr->values[0])) {
                    auto &cmd = std::get<resp::RespBulkString>(arr->values[0]);
                    if (cmd.value) {
                        LOG_INFO("客户端 #{} 执行命令: '{}'", client_fd, *cmd.value);
                    }
                }
            }

            // 处理命令
            std::string response;
            // 检查是否是事务相关命令或者在事务中
            if (std::holds_alternative<std::unique_ptr<resp::RespArray>>(result.value())) {
                auto &arr = std::get<std::unique_ptr<resp::RespArray>>(result.value());
                if (!arr->values.empty() && std::holds_alternative<resp::RespBulkString>(arr->values[0])) {
                auto &cmd = std::get<resp::RespBulkString>(arr->values[0]);
                if (cmd.value) {
                    std::string cmd_str = *cmd.value;
                    // 转换为大写进行比较
                    std::string cmd_upper;
                    cmd_upper.reserve(cmd_str.length());
                    for (char c : cmd_str) {
                        cmd_upper += std::toupper(c);
                    }

                    if (cmd_upper == "MULTI") {
                    // 开始事务
                    if (conn.state == ConnectionState::Normal) {
                        conn.state = ConnectionState::InTransaction;
                        conn.transaction_queue.clear();
                        response = resp::serialize_ok();
                        LOG_INFO("客户端 #{} 开启事务", client_fd);
                    } else {
                        response = resp::serialize_error("ERR MULTI calls can not be nested");
                        }
                    } else if (cmd_upper == "EXEC") {
                        // 执行事务
                        if (conn.state == ConnectionState::InTransaction) {
                            // 执行所有队列中的命令
                            LOG_INFO("客户端 #{} 执行事务，包含 {} 条命令", client_fd, conn.transaction_queue.size());
                            response = kv_server_.execute_transaction(conn.transaction_queue);
                            conn.state = ConnectionState::Normal;
                            conn.transaction_queue.clear();
                        } else {
                            response = resp::serialize_error("ERR EXEC without MULTI");
                        }
                    } else if (cmd_upper == "DISCARD") {
                    // 丢弃事务
                    if (conn.state == ConnectionState::InTransaction) {
                        conn.state = ConnectionState::Normal;
                        conn.transaction_queue.clear();
                        response = resp::serialize_ok();
                        LOG_INFO("客户端 #{} 丢弃事务", client_fd);
                    } else {
                        response = resp::serialize_error("ERR DISCARD without MULTI");
                        }
                    } else if (conn.state == ConnectionState::InTransaction) {
                        // 事务中的命令，加入队列而不是立即执行
                        conn.transaction_queue.push_back(std::move(result.value()));
                        response = resp::serialize_simple_string("QUEUED");
                        LOG_DEBUG("客户端 #{} 在事务中排队命令", client_fd);
                    } else {
                        // 普通命令执行
                        response = kv_server_.execute_command(result.value(), false);
                        }
                    } else {
                        response = kv_server_.execute_command(result.value(), false);
                        }
                } else {
                    response = kv_server_.execute_command(result.value(), false);
                    }
            } else {
                response = kv_server_.execute_command(result.value(), false);
                }

            write(client_fd, response.c_str(), response.length());

        } else {
            // 解析失败
            const auto &error = result.error();
            if (error == resp::ParseError::Incomplete) {
                // 数据不完整，说明需要等待更多网络数据，直接跳出循环
                LOG_DEBUG("客户端 #{} 数据不完整，等待更多数据", client_fd);
                break;
            } else {
                // 发生协议错误，向客户端发送错误信息并关闭连接
                std::string err_msg = "ERR Protocol error";
                switch (error) {
                case resp::ParseError::InvalidType:
                    err_msg += ": invalid type character";
                    break;
                case resp::ParseError::InvalidLength:
                    err_msg += ": invalid length format";
                    break;
                case resp::ParseError::MalformedInteger:
                    err_msg += ": malformed integer";
                    break;
                default:
                    break;
                }
                LOG_ERROR("客户端 #{} 协议错误: {}", client_fd, err_msg);
                std::string serialized_err = resp::serialize_error(err_msg);
                write(client_fd, serialized_err.c_str(), serialized_err.length());

                // 出于健壮性考虑，协议错误后关闭连接
                close_client_connection(client_fd);
                return; // 直接返回，不再处理此客户端的任何数据
            }
        }
    }
}