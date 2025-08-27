module;
#include <algorithm>
#include <charconv> // 引入 from_chars
#include <fstream>
#include <string>
#include <unordered_map>

export module config;
import logger;

// 配置管理类 (单例)
export class Config {
public:
    // 获取 Config 的单例实例
    static Config &instance() {
        static Config config;
        return config;
    }

    // 从文件加载配置
    bool load(const std::string &filename) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            LOG_WARN("无法打开配置文件: {}", filename);
            return false;
        }

        std::string line;
        while (std::getline(file, line)) {
            // 去除首尾空白
            line = trim(line);
            // 跳过空行和注释行
            if (line.empty() || line[0] == '#')
                continue;

            // 寻找第一个空格作为键和值的分隔符
            auto delimiter_pos = line.find(' ');
            if (delimiter_pos != std::string::npos) {
                std::string key = line.substr(0, delimiter_pos);
                std::string value = trim(line.substr(delimiter_pos + 1));
                values_[key] = value;
                LOG_DEBUG("配置项加载: {} = {}", key, value);
            }
        }
        LOG_INFO("配置文件 {} 加载成功，共 {} 项配置", filename, values_.size());
        return true;
    }

    // 获取字符串类型的配置项，可提供默认值
    std::string get_string(const std::string &key, const std::string &default_value = "") const {
        auto it = values_.find(key);
        return it == values_.end() ? default_value : it->second;
    }

    // 获取整数类型的配置项，可提供默认值
    int get_int(const std::string &key, int default_value = 0) const {
        auto it = values_.find(key);
        if (it == values_.end()) {
            return default_value;
        }

        // 使用 std::from_chars，高性能、无异常
        int value;
        auto [ptr, ec] = std::from_chars(it->second.data(), it->second.data() + it->second.size(), value);

        if (ec == std::errc() && ptr == it->second.data() + it->second.size()) {
            return value;
        }

        LOG_WARN("无法将键 '{}' 的值 '{}' 解析为整数", key, it->second);
        return default_value;
    }

private:
  // 单例模式
  Config() = default;
  ~Config() = default;
  Config(const Config &) = delete;
  Config &operator=(const Config &) = delete;

  // 使用哈希表存储键值对
  std::unordered_map<std::string, std::string> values_;

  // 去除字符串首尾的空白字符
  static std::string trim(const std::string &str) {
        const std::string whitespace = " \t";
        const auto strBegin = str.find_first_not_of(whitespace);
        if (strBegin == std::string::npos)
            return ""; // 字符串全是空白
        const auto strEnd = str.find_last_not_of(whitespace);
        const auto strRange = strEnd - strBegin + 1;
        return str.substr(strBegin, strRange);
    }
};