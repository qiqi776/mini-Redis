#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <functional>

// c++20异构
// 1、定义一个哈希函数
struct string_hash {
    using is_transparent = void;//利用别名标记为透明
    size_t operator()(std::string_view sv) const {
        return std::hash<std::string_view>{}(sv);
    }
};
// 2、标准库中已有的透明的等价比较函数
using string_equal_to = std::equal_to<>;
// 3、定义一个存储类型
using Storage = std::unordered_map<std::string, std::string, string_hash, string_equal_to>;

// 定义一个辅助函数分割字符串，返回string_view
std::vector<std::string_view> split(std::string_view s){
    std::vector<std::string_view> tokens;
    size_t start = 0;
    size_t end = 0;
    while ((end = s.find(' ', start)) != std::string_view::npos) {
        if(end > start){
            tokens.push_back(s.substr(start, end - start));
        }
        start = end + 1;
    }
    if(start < s.length()){
        tokens.push_back(s.substr(start));
    }
    return tokens;
}

// 实际处理指令的函数
void process_command(std::string_view request, Storage& db){
    std::cout << "> " << request << std::endl;

    auto tokens = split(request);// 分割
    if(tokens.empty()) {
        return;
    }

    const auto& command = tokens[0];// 分拣
    if (command == "SET") {
        if (tokens.size() == 3) {
            // 当需要将 string_view 作为 key 或 value 存入 map 时，
            //必须创建一个新的std::string，因为map需要拥有数据的所有权。
            db[std::string(tokens[1])] = std::string(tokens[2]);
            std::cout << "Ok" << std::endl;
        } else {
            std::cout << "Error: wrong number of arguments for 'SET' command" << std::endl;
        }
    } else if (command == "GET") {
        if (tokens.size() == 2) {
            //完美！现在我们可以直接用string_view进行查找，实现零拷贝！
            //C++20标准和我们现代化的工具链允许我们这样做，
            //只要我们像上面那样为unordered_map提供了透明的哈希和比较函数。
            if (auto it = db.find(tokens[1]); it != db.end()) {
                std::cout << "\"" << it->second << "\"" << std::endl;
            } else {
                std::cout << "(nil)" << std::endl;
            }
        } else {
            std::cout << "Error: wrong number of arguments for 'GET' command" << std::endl;
        }
    } else {
        std::cout << "Error: unknown command '" << command << "'" << std::endl;
    }
    std::cout << "..." << std::endl;
}

int main(){
    Storage db;

    std::vector<std::string> requests = {
        "SET name Alice",
        "GET name",
        "GET age 30",
        "SET age",
        "GET non_exist_key",
        "SET",
        "GET key value"
    };

    for (const auto& req : requests) {
        process_command(req, db);
    }

    return 0;
}