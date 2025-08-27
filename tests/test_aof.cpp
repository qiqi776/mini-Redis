// tests/test_aof.cpp
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

// 导入所需的 C++23 模块。
// 对于测试构建，如果构建系统不能很好地为可执行文件处理模块，
// 这通常意味着包含实现文件。
// 我们假设构建系统能够正确处理模块导入。
import aof;
import config;
import kv_server;
import resp;

// 测试 AOF 的追加功能
void test_aof_append() {
  std::cout << "--- 正在测试: AOF 追加功能 ---" << std::endl;
  const std::string aof_filename = "test_append.aof";
  // 清理可能存在的旧测试文件
  std::filesystem::remove(aof_filename);

  // 1. 初始化 AOF 记录器和服务器
  Aof aof_logger(aof_filename);
  KVServer server;
  server.set_aof(&aof_logger);

  // 2. 使用移动语义构造一个 SET 命令
  resp::RespArray command_array;
  command_array.values.push_back(
      resp::RespValue(resp::RespBulkString{{"SET"}}));
  command_array.values.push_back(
      resp::RespValue(resp::RespBulkString{{"key1"}}));
  command_array.values.push_back(
      resp::RespValue(resp::RespBulkString{{"value1"}}));
  auto command = resp::RespValue(
      std::make_unique<resp::RespArray>(std::move(command_array)));

  // 3. 执行命令，这将触发 AOF 追加
  server.execute_command(command, false);

  // 4. 验证 AOF 文件的内容是否正确
  std::ifstream aof_file(aof_filename);
  std::string content((std::istreambuf_iterator<char>(aof_file)),
                      std::istreambuf_iterator<char>());

  std::string expected_content = resp::serialize(command);
  assert(content == expected_content);
  std::cout << "  [通过] AOF 文件内容正确。" << std::endl;

  // 5. 清理测试文件
  aof_file.close();
  std::filesystem::remove(aof_filename);
}

// 测试从 AOF 文件加载数据
void test_aof_load() {
  std::cout << "--- 正在测试: AOF 加载功能 ---" << std::endl;
  const std::string aof_filename = "test_load.aof";
  // 清理可能存在的旧测试文件
  std::filesystem::remove(aof_filename);

  // 1. 准备一个包含几条命令的 AOF 文件
  resp::RespArray cmd1_array;
  cmd1_array.values.push_back(resp::RespValue(resp::RespBulkString{{"SET"}}));
  cmd1_array.values.push_back(resp::RespValue(resp::RespBulkString{{"name"}}));
  cmd1_array.values.push_back(resp::RespValue(resp::RespBulkString{{"jerry"}}));
  auto cmd1 =
      resp::RespValue(std::make_unique<resp::RespArray>(std::move(cmd1_array)));

  resp::RespArray cmd2_array;
  cmd2_array.values.push_back(resp::RespValue(resp::RespBulkString{{"SET"}}));
  cmd2_array.values.push_back(resp::RespValue(resp::RespBulkString{{"age"}}));
  cmd2_array.values.push_back(resp::RespValue(resp::RespBulkString{{"25"}}));
  auto cmd2 =
      resp::RespValue(std::make_unique<resp::RespArray>(std::move(cmd2_array)));

  std::ofstream out_aof(aof_filename);
  out_aof << resp::serialize(cmd1);
  out_aof << resp::serialize(cmd2);
  out_aof.close();

  // 2. 创建 AOF 记录器并加载命令
  Aof aof_logger(aof_filename);
  auto loaded_commands = aof_logger.load_commands();
  assert(loaded_commands.size() == 2);
  std::cout << "  [通过] 加载了正确数量的命令。" << std::endl;

  // 3. 创建一个新服务器并执行加载的命令
  KVServer server;
  for (const auto &cmd : loaded_commands) {
    server.execute_command(cmd, true); // `true` 表示命令来自 AOF，不应再次追加
  }

  // 4. 验证服务器状态是否已正确恢复
  resp::RespArray get_cmd_name_array;
  get_cmd_name_array.values.push_back(
      resp::RespValue(resp::RespBulkString{{"GET"}}));
  get_cmd_name_array.values.push_back(
      resp::RespValue(resp::RespBulkString{{"name"}}));
  auto get_cmd_name = resp::RespValue(
      std::make_unique<resp::RespArray>(std::move(get_cmd_name_array)));

  resp::RespArray get_cmd_age_array;
  get_cmd_age_array.values.push_back(
      resp::RespValue(resp::RespBulkString{{"GET"}}));
  get_cmd_age_array.values.push_back(
      resp::RespValue(resp::RespBulkString{{"age"}}));
  auto get_cmd_age = resp::RespValue(
      std::make_unique<resp::RespArray>(std::move(get_cmd_age_array)));

  std::string res_name = server.execute_command(get_cmd_name, true);
  std::string res_age = server.execute_command(get_cmd_age, true);

  assert(res_name == resp::serialize_bulk_string("jerry"));
  std::cout << "  [通过] 正确恢复了 'name' 的值。" << std::endl;
  assert(res_age == resp::serialize_bulk_string("25"));
  std::cout << "  [通过] 正确恢复了 'age' 的值。" << std::endl;

  // 5. 清理测试文件
  std::filesystem::remove(aof_filename);
}

// 测试加载空的 AOF 文件
void test_empty_aof_load() {
  std::cout << "--- 正在测试: 加载空 AOF 文件 ---" << std::endl;
  const std::string aof_filename = "test_empty.aof";
  // 清理可能存在的旧测试文件
  std::filesystem::remove(aof_filename);

  // 创建一个空文件
  std::ofstream(aof_filename).close();

  Aof aof_logger(aof_filename);
  auto commands = aof_logger.load_commands();

  assert(commands.empty());
  std::cout << "  [通过] 加载空 AOF 文件应返回零条命令。" << std::endl;

  // 清理测试文件
  std::filesystem::remove(aof_filename);
}

int main() {
  std::cout << "--- 开始 AOF 单元测试 ---" << std::endl;
  test_aof_append();
  test_aof_load();
  test_empty_aof_load();
  std::cout << "--- AOF 单元测试全部通过 ---" << std::endl;
  return 0;
}