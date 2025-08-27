# 设置要求的最低 CMake 版本
cmake_minimum_required(VERSION 3.28)
# 定义项目名称和语言
project(mini_redis CXX)

# 设置 C++ 标准为 23，并启用模块支持
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- 模块定义 ---
# 采用现代 CMake 的标准方式定义每个模块

# 1. resp 模块
add_library(resp)
target_sources(resp PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES FILES src/resp.cppm)

# 2. 高性能缓冲区模块
add_library(buffer)
target_sources(buffer PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES FILES src/buffer.cppm)

# 3. 日志模块
add_library(logger)
target_sources(logger PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES FILES src/logger.cppm)

# 4. 配置模块
add_library(config)
target_sources(config PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES FILES src/config.cppm)
target_link_libraries(config PUBLIC logger)

# 5. server_stat 模块
add_library(server_stat)
target_sources(server_stat PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES FILES src/server_stat.cppm)

# 6. 定时器模块
add_library(timer)
target_sources(timer PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES FILES src/timer.cppm)
target_link_libraries(timer PUBLIC logger)

# 7. aof 模块
add_library(aof)
target_sources(aof PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES FILES src/aof.cppm)
target_link_libraries(aof PUBLIC logger resp)

# 8. command 模块
add_library(command)
target_sources(command PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES
  FILES
    src/command/command.cppm
    src/command/command_defs.cppm
    src/command/command_factory.cppm
    src/command/get_command.cppm
    src/command/set_command.cppm
    src/command/expire_command.cppm
    src/command/pexpire_command.cppm
    src/command/ttl_command.cppm
    src/command/pttl_command.cppm
    src/command/persist_command.cppm
    src/command/info_command.cppm
    src/command/unknown_command.cppm
)
target_link_libraries(command PUBLIC resp logger aof server_stat)

# 9. kv_server 模块
add_library(kv_server)
target_sources(kv_server PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES FILES src/kv_server.cppm)
target_link_libraries(kv_server PUBLIC resp logger aof server_stat timer command)

# 10. epoll_server 模块
add_library(epoll_server)
target_sources(epoll_server PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES FILES src/epoll_server.cppm)
target_link_libraries(epoll_server PUBLIC kv_server buffer logger timer)

# 11. application模块
add_library(application)
target_sources(application PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES FILES src/application.cppm)
target_link_libraries(application PUBLIC epoll_server config timer aof)

# 12. 通用客户端工具模块
add_library(client_utils)
target_sources(client_utils PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES FILES src/common/client_utils.cppm)
target_link_libraries(client_utils PUBLIC resp)


# --- 可执行文件定义 ---

# 服务器主程序
add_executable(server src/main.cpp)
# 服务器程序导入了 application 模块
target_link_libraries(server PRIVATE application)

# 客户端工具
add_executable(client tools/client.cpp)
# 客户端工具导入了 resp 和 client_utils 模块
target_link_libraries(client PRIVATE client_utils)

# 自动化测试工具
add_executable(auto_tester tools/auto_tester.cpp)
target_link_libraries(auto_tester PRIVATE client_utils)

# 事务测试工具
add_executable(transaction_tester tools/transaction_tester.cpp)
target_link_libraries(transaction_tester PRIVATE client_utils)

# 统计信息测试工具
add_executable(stat_tester tools/stat_tester.cpp)
target_link_libraries(stat_tester PRIVATE client_utils)

# 性能测试工具
add_executable(performance_tester tools/performance_tester.cpp)
target_link_libraries(performance_tester PRIVATE client_utils pthread)

# --- 单元测试 ---
enable_testing()

add_executable(test_buffer tests/test_buffer.cpp)
target_link_libraries(test_buffer PRIVATE buffer)

add_test(NAME BufferTest COMMAND test_buffer)

# AOF Test
add_executable(test_aof tests/test_aof.cpp)
target_link_libraries(test_aof PRIVATE application) # Linking against application pulls in all other necessary modules
add_test(NAME AofTest COMMAND test_aof)

# Timer Test
add_executable(test_timer tests/test_timer.cpp)
target_link_libraries(test_timer PRIVATE timer logger)
add_test(NAME TimerTest COMMAND test_timer)

# Comprehensive Timer Test
add_executable(test_timer_comprehensive tests/test_timer_comprehensive.cpp)
target_link_libraries(test_timer_comprehensive PRIVATE timer logger pthread)
add_test(NAME TimerComprehensiveTest COMMAND test_timer_comprehensive)

# AOF Sync Strategy Test
add_executable(test_aof_sync tests/test_aof_sync.cpp)
target_link_libraries(test_aof_sync PRIVATE aof timer logger)
add_test(NAME AofSyncTest COMMAND test_aof_sync)

# AOF Performance Test
add_executable(test_aof_performance tests/test_aof_performance.cpp)
target_link_libraries(test_aof_performance PRIVATE aof timer logger)
add_test(NAME AofPerformanceTest COMMAND test_aof_performance)

# Key Expiration Test
add_executable(test_key_expiration tests/test_key_expiration.cpp)
target_link_libraries(test_key_expiration PRIVATE kv_server timer logger)
add_test(NAME KeyExpirationTest COMMAND test_key_expiration)

# Transaction Test
add_executable(test_transaction tests/test_transaction.cpp)
target_link_libraries(test_transaction PRIVATE kv_server resp)
add_test(NAME TransactionTest COMMAND test_transaction)

# Integration Test
add_executable(test_integration tests/test_integration.cpp)
target_link_libraries(test_integration PRIVATE application)
add_test(NAME IntegrationTest COMMAND test_integration)