module;

#include <cerrno>
#include <chrono>
#include <cstring>
#include <functional>
#include <memory>
#include <set>
#include <sys/timerfd.h>
#include <unistd.h>

export module timer;
import logger;

// 回调函数
export using TimerCallback = std::function<void()>;

export class Timer {
public:
    explicit Timer(
        std::chrono::milliseconds when, TimerCallback cb, bool repeat = false,
        std::chrono::milliseconds interval = std::chrono::milliseconds(0))
        : expiration_(when), callback_(std::move(cb)), repeat_(repeat), interval_(interval) {}

    // 获取过期时间点
    std::chrono::milliseconds expiration() const { return expiration_; }

    void run() const {
        if(callback_) {
            callback_();
        }
    }

    // 是否是重复定时器
    bool repeat() const { return repeat_; }

    // 对于重复响的闹钟，需要重新计算下一次响铃的时间
    void restart() {
        if(repeat_) {
            expiration_ += interval_;
        }
    }

    // 手动重置定时器时间
    void reset(std::chrono::milliseconds when) {
        expiration_ = when;
    }
private:
    TimerCallback callback_;               // 定时器回调
    bool repeat_;                          // 是否重复
    std::chrono::milliseconds expiration_; // 过期时间
    std::chrono::milliseconds interval_;   // 定时器间隔
};

struct TimerCmp {
    bool operator()(const std::unique_ptr<Timer> &lhs,
                    const std::unique_ptr<Timer> &rhs) const {
        return lhs->expiration() < rhs->expiration();
    }
};

// 定时器队列
export class TimerQueue {
public:
    TimerQueue();
    ~TimerQueue();
    int timer_fd() const { return timer_fd_; } // 获取 timer_fd
    Timer *add_timer(std::chrono::milliseconds when, TimerCallback cb, bool repeat = false,
                     std::chrono::milliseconds interval = std::chrono::milliseconds(0));           // 添加新定时器
    void process_timer_event(); // 处理定时器事件

private:
    int timer_fd_; // timerfd 文件描述符
    void reset_timerfd(); // 重置 timerfd
    std::chrono::milliseconds now(); // 获取当前时间
    std::set<std::unique_ptr<Timer>, TimerCmp> timers_; // 定时器集合
};

TimerQueue::TimerQueue(){
    timer_fd_ = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if(timer_fd_ < 0){
        LOG_FATAL("创建 timerfd 失败: {}", std::strerror(errno));
    }
    LOG_INFO("Timerfd 创建成功: {}", timer_fd_);
}
TimerQueue::~TimerQueue(){
    if(timer_fd_ >= 0){
        close(timer_fd_);
        LOG_DEBUG("Timerfd 关闭成功: {}", timer_fd_);
    }
}

// 获取当前时间
std::chrono::milliseconds TimerQueue::now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch());
}

// 添加定时器
Timer *TimerQueue::add_timer(std::chrono::milliseconds when, TimerCallback cb, bool repeat,
                             std::chrono::milliseconds interval) {
    auto timer = std::make_unique<Timer>(when + now(), std::move(cb), repeat, interval);
    Timer *timer_ptr = timer.get();
    // 检查新添加的定时器是否会成为新的最早过期的定时器
    bool earliest = false;
    if (timers_.empty() || when < (*timers_.begin())->expiration()) {
        earliest = true;
    }
    // 添加到定时器集合
    timers_.insert(std::move(timer));
    // 如果新添加的定时器是最早过期的，重置 timerfd
    if (earliest) {
        reset_timerfd();
    }
    return timer_ptr;
}

void TimerQueue::reset_timerfd() {
    if(timers_.empty()) {
        return;
    }
    // 获取最早过期的定时器
    const auto &earliest = *timers_.begin();
    // 设置timerfd需要的itimerspec结构体
    struct itimerspec new_value;
    struct itimerspec old_value;
    // 先清零
    memset(&new_value, 0, sizeof(new_value));
    memset(&old_value, 0, sizeof(old_value));
    // 计算还有多久过期
    auto expiration = earliest->expiration();
    auto current = now();
    std::chrono::milliseconds timeout;

    if(expiration > current) {
        timeout = expiration - current; // 如果没有到期，计算时间差
    } else {
        timeout = std::chrono::milliseconds(1); // 如果过期，立即触发
    }
    // 把c++的milliseconds转换为c的timespec结构
    new_value.it_value.tv_sec = timeout.count() / 1000;
    new_value.it_value.tv_nsec = (timeout.count() % 1000) * 1000000;
    // 正式设置内核定时器
    int ret = timerfd_settime(timer_fd_, 0, &new_value, nullptr);
    if(ret < 0) {
        LOG_FATAL("重置 timerfd 失败: {}", std::strerror(errno));
    }
}

void TimerQueue::process_timer_event() {
    // 读取timerfd，清除可读事件
    uint64_t howmany;
    ssize_t n = read(timer_fd_, &howmany, sizeof(howmany));
    if(n != sizeof(howmany)) {
        LOG_ERROR("读取 timerfd 失败: 读取了{}个字节，而不是 {}", n, sizeof(howmany));
    }

    // 获取当前时间
    auto current = now();
    // 存储已过期的定时器
    std::vector<std::unique_ptr<Timer>> expired;
    // 找出所有已到期的定时器
    auto it = timers_.begin();
    while (it != timers_.end() && (*it)->expiration() <= current) {
        expired.push_back(std::move(const_cast<std::unique_ptr<Timer> &>(*it)));
        it = timers_.erase(it);
    }
    // 处理到期的定时器
    for (auto &timer : expired) {
        timer->run();

        // 对于重复定时器，重新计算时间并加入队列
        if (timer->repeat()) {
            timer->restart();
            timers_.insert(std::move(timer));
        }
    }

    // 重置timerfd
    if (!timers_.empty()) {
        reset_timerfd();
    }
}