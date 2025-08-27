module;
#include <charconv>
#include <expected>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

export module resp;

export namespace resp {

// 为了处理递归 variant，需要导出前向声明
struct RespArray;
struct RespBulkString;

// 定义具体 RESP 类型
struct RespSimpleString {
    std::string value;
};
struct RespError {
    std::string value;
};
struct RespInteger {
    long long value;
};

// BulkString 可以为 null，用 optional 表示
struct RespBulkString {
    std::optional<std::string> value;
};
struct RespNull {};

// 使用 unique_ptr 来打破 RespValue 和 RespArray 之间的递归
using RespValue =
    std::variant<RespSimpleString, RespError, RespInteger, RespBulkString,
                 std::unique_ptr<RespArray>, RespNull>;

struct RespArray {
    std::vector<RespValue> values;
};

// --- 解析器 ---

// 定义解析可能出现的错误类型
enum class ParseError {
    Incomplete,       // 数据不完整
    InvalidType,      // 未知的类型前缀
    InvalidLength,    // 无效的长度值
    MalformedInteger, // 整数格式错误
};

// 主解析函数，返回一个包含 RespValue 或 ParseError 的 expected
std::expected<RespValue, ParseError> parse(std::string_view &input);

// --- 序列化 ---

// 将任何 RespValue 变体序列化为字符串
std::string serialize(const RespValue &value);

// 为方便使用，提供独立的序列化辅助函数
std::string serialize_simple_string(const std::string &s);
std::string serialize_bulk_string(const std::string &s);
std::string serialize_error(const std::string &s);
std::string serialize_null_bulk_string();   // 用于表示 (nil)
std::string serialize_ok();                 // "+OK\r\n"
std::string serialize_integer(long long n); // 用于整数回复
std::string serialize_array(const std::vector<RespValue> &values); // 用于数组回复
} // namespace resp

// --- 实现 ---
namespace resp {
// 序列化辅助函数
std::string serialize_simple_string(const std::string &s) {
    return std::format("+{}\r\n", s);
}
std::string serialize_bulk_string(const std::string &s) {
    return std::format("${}\r\n{}\r\n", s.length(), s);
}
std::string serialize_error(const std::string &s) {
    return std::format("-{}\r\n", s);
}
std::string serialize_null_bulk_string() { return "$-1\r\n"; }
std::string serialize_ok() { return "+OK\r\n"; }
std::string serialize_integer(long long n) { return std::format(":{}\r\n", n); }


// 新增：序列化RESP数组的辅助函数
std::string serialize_array(const std::vector<RespValue> &values) {
    std::string result = std::format("*{}\r\n", values.size());
    for (const auto &value : values) {
        result += serialize(value);
    }
    return result;
}

// 主序列化函数，使用 std::visit
std::string serialize(const RespValue &value) {
    return std::visit(
        [](const auto &val) -> std::string {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, RespSimpleString>) {
                return serialize_simple_string(val.value);
            } else if constexpr (std::is_same_v<T, RespError>) {
                return serialize_error(val.value);
            } else if constexpr (std::is_same_v<T, RespInteger>) {
                return std::format(":{}\r\n", val.value);
            } else if constexpr (std::is_same_v<T, RespBulkString>) {
                if (val.value) {
                    return serialize_bulk_string(*val.value);
                } else {
                    return serialize_null_bulk_string();
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<RespArray>>) {
                std::string result = std::format("*{}\r\n", val->values.size());
                for (const auto &elem : val->values) {
                    result += serialize(elem);
            }
                return result;
            } else if constexpr (std::is_same_v<T, RespNull>) {
                return serialize_null_bulk_string(); // RESP v2 中 Null 用 Bulk String
                                                // 的 $-1 表示
            }
            return ""; // Should not happen
        },
        value);
}

// 解析器实现细节
namespace {
// 从输入中安全地读取一行
std::optional<std::string_view> read_line(std::string_view &input) {
    size_t pos = input.find("\r\n");
    if (pos == std::string_view::npos) {
        return std::nullopt;
    }
    std::string_view line = input.substr(0, pos);
    input.remove_prefix(pos + 2);
    return line;
}

// 安全地将 string_view 转换为 long long
std::optional<long long> to_long(std::string_view s) {
    long long value;
    auto result = std::from_chars(s.data(), s.data() + s.size(), value);
    if (result.ec == std::errc() && result.ptr == s.data() + s.size()) {
        return value;
    }
    return std::nullopt;
}

// 递归地解析单个 RESP 消息
std::expected<RespValue, ParseError> parse_message(std::string_view &input) {
    if (input.empty()) {
        return std::unexpected(ParseError::Incomplete);
    }

    char type_char = input[0];
    input.remove_prefix(1); // Consume type character

    auto line_opt = read_line(input);
    if (!line_opt) {
        // 将类型字符加回去，因为我们无法完成解析
        input = std::string_view(input.data() - 1, input.length() + 1);
        return std::unexpected(ParseError::Incomplete);
    }
    std::string_view line = *line_opt;

    switch (type_char) {
    case '+': {
        return RespSimpleString{std::string(line)};
    }
    case '-': {
        return RespError{std::string(line)};
    }
    case ':': {
        auto num = to_long(line);
        if (!num)
            return std::unexpected(ParseError::MalformedInteger);
        return RespInteger{*num};
    }
    case '$': {
        auto len_opt = to_long(line);
        if (!len_opt)
            return std::unexpected(ParseError::InvalidLength);
        long long len = *len_opt;

        if (len == -1) {
            return RespBulkString{std::nullopt};
        }
        if (input.size() < static_cast<size_t>(len + 2)) {
            return std::unexpected(ParseError::Incomplete);
        }
        std::string data(input.substr(0, len));
        // 验证随后的 "\r\n"
        if (input.substr(len, 2) != "\r\n") {
            return std::unexpected(ParseError::InvalidLength);
        }
        input.remove_prefix(len + 2);
        return RespBulkString{std::move(data)};
    }
    case '*': {
        auto len_opt = to_long(line);
        if (!len_opt)
            return std::unexpected(ParseError::InvalidLength);
        long long len = *len_opt;

        auto array = std::make_unique<RespArray>();
        for (long long i = 0; i < len; ++i) {
            auto element = parse(input); // 注意这里是调用外部的 parse
            if (element) {
                array->values.push_back(std::move(*element));
            } else {
                return std::unexpected(element.error());
            }
        }
        return array;
    }
    default:
        return std::unexpected(ParseError::InvalidType);
    }
}
} // namespace

// 主解析函数，它处理回滚逻辑
std::expected<RespValue, ParseError> parse(std::string_view &input) {
    std::string_view original_input = input;
    auto result = parse_message(input);

    if (!result) {
        // 如果解析失败（无论是数据不完整还是格式错误），都回滚 input
        input = original_input;
    }

    return result;
}
} // namespace resp