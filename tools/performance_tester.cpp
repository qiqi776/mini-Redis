#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

// 导入新的通用客户端工具模块
import client_utils;

std::atomic<int> successful_connections{0};
std::atomic<int> failed_connections{0};
std::atomic<long long> total_bytes_sent{0};
std::atomic<long long> total_bytes_received{0};
std::atomic<long long> successful_queries{0};

void client_worker(const char *ip, int port, int messages_per_client) {
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock == -1) {
    failed_connections++;
    return;
  }

  sockaddr_in serv_addr;
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(port);

  if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) <= 0) {
    failed_connections++;
    close(sock);
    return;
  }

  // 设置一个短暂的连接超时，防止在测试阻塞服务器时永远等待
  struct timeval timeout;
  timeout.tv_sec = 2; // 2秒超时
  timeout.tv_usec = 0;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

  if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
    failed_connections++;
    close(sock);
    return;
  }

  successful_connections++;

  const std::string message =
      common::ClientUtils::serialize_command("SET key value");
  char buffer[1024] = {0};

  for (int i = 0; i < messages_per_client; ++i) {
    if (send(sock, message.c_str(), message.length(), 0) < 0) {
      break;
    }
    total_bytes_sent += message.length();

    int bytes_received = read(sock, buffer, 1024);
    if (bytes_received > 0) {
      total_bytes_received += bytes_received;
      successful_queries++;
    } else {
      // 读取超时或对方关闭，也算作循环结束
      break;
    }
  }

  close(sock);
}

int main(int argc, char *argv[]) {
  if (argc != 5) {
    std::cerr << "用法: " << argv[0]
              << " <ip> <端口> <客户端数量> <每个客户端的消息数>" << std::endl;
    return 1;
  }

  const char *ip = argv[1];
  int port = std::stoi(argv[2]);
  int num_clients = std::stoi(argv[3]);
  int messages_per_client = std::stoi(argv[4]);

  std::cout << "--- 性能测试开始 ---" << std::endl;
  std::cout << "服务器 IP: " << ip << std::endl;
  std::cout << "服务器端口: " << port << std::endl;
  std::cout << "客户端数量: " << num_clients << std::endl;
  std::cout << "每个客户端发送的消息数: " << messages_per_client << std::endl;
  std::cout << "----------------------" << std::endl;

  std::vector<std::thread> threads;
  auto start_time = std::chrono::high_resolution_clock::now();

  for (int i = 0; i < num_clients; ++i) {
    threads.emplace_back(client_worker, ip, port, messages_per_client);
  }

  for (auto &t : threads) {
    t.join();
  }

  auto end_time = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> elapsed_seconds = end_time - start_time;

  std::cout << "\n--- 性能测试结果 ---" << std::endl;
  std::cout << "总耗时: " << std::fixed << std::setprecision(2)
            << elapsed_seconds.count() << " 秒" << std::endl;
  std::cout << "成功连接数: " << successful_connections << std::endl;
  std::cout << "失败连接数: " << failed_connections << std::endl;
  std::cout << "成功处理的请求数: " << successful_queries << std::endl;
  std::cout << "总发送字节: " << total_bytes_sent << " bytes" << std::endl;
  std::cout << "总接收字节: " << total_bytes_received << " bytes" << std::endl;
  if (elapsed_seconds.count() > 0) {
    double qps = successful_queries / elapsed_seconds.count();
    std::cout << "每秒查询数 (QPS): " << std::fixed << std::setprecision(2)
              << qps << " (基于成功处理的请求-响应)" << std::endl;
  }
  std::cout << "----------------------" << std::endl;

  return 0;
}