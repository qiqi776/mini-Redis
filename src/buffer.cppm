module;
#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <string>
#include <string_view>
#include <sys/uio.h>
#include <unistd.h>
#include <vector>

export module buffer;

export class Buffer {
public:
    // 在缓冲区头部预留廉价空间
    static constexpr size_t kCheapPrepend = 8;
    // 缓冲区初始大小
    static constexpr size_t kInitialSize = 1024;
    
    explicit Buffer(size_t initial_size = kInitialSize)
        : buffer_(kCheapPrepend + initial_size), reader_index_(kCheapPrepend), writer_index_(kCheapPrepend) {}
    // 返回可读数据的字节数
    size_t readable_bytes() const noexcept {
        return writer_index_ - reader_index_;
    }
    // 返回可写空间的字节数
    size_t writable_bytes() const noexcept {
        return buffer_.size() - writer_index_;
    }
    // 返回头部预留空间（已消费数据空间）的字节数。
    size_t prependable_bytes() const noexcept {
        return reader_index_;
    }
    // 以 string_view 的形式返回可读数据区域，这是一个只读视图，非常安全且高效。
    std::string_view readable_view() const noexcept {
        return {peek(), readable_bytes()};
    }

    // 返回指向可读数据头部的指针
    const char *peek() const noexcept {
        return begin() + reader_index_;
    }
    // 在可读数据中查找CRLF
    const char *find_crlf() const noexcept;

    // 从可读数据区消费（丢弃）指定长度的数据。
    void retrieve(size_t len) noexcept;
    // 消费数据直到指定的地址。
    void retrieve_until(const char *end) noexcept;
    // 消费所有可读数据。
    void retrieve_all() noexcept;
    // 消费指定长度的数据，并将其作为 std::string 返回。
    std::string retrieve_as_string(size_t len);

    // 向缓冲区追加数据。
    void append(std::string_view data);

    // 从文件描述符（如 socket）读取数据到缓冲区。使用 readv
    // 进行分散-聚集I/O以提高效率。
    ssize_t read_fd(int fd, int *saved_errno);

private:
    // 获取整个缓冲区存储区的起始地址（非常量版本）。
    char *begin() noexcept { return &*buffer_.begin(); }
    // 获取整个缓冲区存储区的起始地址（常量版本）。
    const char *begin() const noexcept { return &*buffer_.begin(); }
    // 获取可写区域的起始地址。
    char *begin_write() noexcept { return begin() + writer_index_; }

    // 确保缓冲区有足够的可写空间。
    void ensure_writable_bytes(size_t len);
    // 在需要时为缓冲区腾出空间，可能会移动现有数据或进行扩容。
    void make_space(size_t len);

    // 底层的存储，使用 std::vector<char>。
    std::vector<char> buffer_;
    // 读索引。记录可读数据的起始位置。
    size_t reader_index_;
    // 写索引。记录可写区域的起始位置。
    size_t writer_index_;
    // CRLF 静态常量。
    static const char kCRLF[];
};

const char Buffer::kCRLF[] = "\r\n";

// 在可读数据区域内查找 "\r\n"。
const char *Buffer::find_crlf() const noexcept {
    // std::search 在一个序列中查找另一个子序列。
    const char *crlf =
        std::search(peek(), begin() + writer_index_, kCRLF, kCRLF + 2);
    // 如果找到的位置是可读数据的末尾，说明没找到，返回 nullptr。
    return crlf == begin() + writer_index_ ? nullptr : crlf;
}

// 消费（即“丢弃”）len 字节的数据。
void Buffer::retrieve(size_t len) noexcept {
    if (len < readable_bytes()) {
        // 如果要消费的长度小于可读数据总长，简单地前移读索引即可。
        reader_index_ += len;
    } else {
        // 否则，消费所有数据。
        retrieve_all();
    }
}

// 消费数据直到 end 指针所在的位置。
void Buffer::retrieve_until(const char *end) noexcept {
    // 计算需要消费的字节数。
    retrieve(end - peek());
}

// 消费所有可读数据，并通过重置读写索引来完成。
void Buffer::retrieve_all() noexcept {
    // 将读写索引都重置到预留空间的末尾，这是最高效的“清空”方式。
    reader_index_ = kCheapPrepend;
    writer_index_ = kCheapPrepend;
}

// 取出 len 字节的数据，并以 std::string 形式返回。
std::string Buffer::retrieve_as_string(size_t len) {
    // 确保不会取出超过可读字节数的数据。
    len = std::min(len, readable_bytes());
    std::string result(peek(), len);
    // 从缓冲区中消费掉已取出的数据。
    retrieve(len);
    return result;
}

// 向缓冲区追加数据。
void Buffer::append(std::string_view data) {
    // 首先确保有足够的空间写入。
    ensure_writable_bytes(data.size());
    // 拷贝数据到可写区域。
    std::copy(data.begin(), data.end(), begin_write());
    // 前移写索引。
    writer_index_ += data.size();
}

// 确保可写空间至少有 len 字节。
void Buffer::ensure_writable_bytes(size_t len) {
    if (writable_bytes() < len) {
        // 如果可写空间不足，调用 make_space 来腾出空间。
        make_space(len);
    }
}

// 为写入腾出空间。这是一个核心优化函数。
void Buffer::make_space(size_t len) {
    if(prependable_bytes() + writable_bytes() < len + kCheapPrepend){
        buffer_.resize(writer_index_ + len + kInitialSize);
    } else {
        size_t readable = readable_bytes();
        std::move(begin() + reader_index_, begin() + writer_index_,
            begin() + kCheapPrepend);
        reader_index_ = kCheapPrepend;
        writer_index_ = reader_index_ + readable;
    }
}

// 从文件描述符 fd 读取数据。
ssize_t Buffer::read_fd(int fd, int *saved_errno) {
    // 使用一个栈上的临时缓冲区，以应对一次读取大量数据的情况。
    char extrabuf[65536];
    struct iovec vec[2];
    const size_t writable = writable_bytes();
    // 第一块 I/O 向量指向 buffer_ 内部的可写空间。
    vec[0].iov_base = begin_write();
    vec[0].iov_len = writable;
    // 第二块 I/O 向量指向栈上的临时空间。
    vec[1].iov_base = extrabuf;
    vec[1].iov_len = sizeof(extrabuf);

    // 如果内部可写空间足够大，就只用一块 I/O 向量，避免不必要的开销。
    const int iovcnt = (writable < sizeof(extrabuf)) ? 2 : 1;
    // 使用 readv 系统调用，它可以一次性将数据读入多个不连续的缓冲区（分散读）。
    const ssize_t n = ::readv(fd, vec, iovcnt);

    if (n < 0) {
        // 读取出错，保存 errno。
        *saved_errno = errno;
    } else if (static_cast<size_t>(n) <= writable) {
        // 如果读取的数据量小于等于内部可写空间，直接移动写索引。
        writer_index_ += n;
    } else {
        // 如果读取的数据量超过了内部可写空间，说明数据也被读到了 extrabuf 中。
        // 先将内部可写空间写满。
        writer_index_ = buffer_.size();
        // 再将 extrabuf 中的数据追加到 buffer_ 中（这会触发扩容）。
        append({extrabuf, static_cast<size_t>(n) - writable});
    }
    return n;
}