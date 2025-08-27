// tests/test_buffer.cpp
#include <cassert>
#include <iostream>
#include <string>
#include <thread>

import buffer;

void test_buffer_append_retrieve() {
  std::cout << "Test: Buffer Append and Retrieve..." << std::endl;
  Buffer buf;
  assert(buf.readable_bytes() == 0);
  assert(buf.writable_bytes() == Buffer::kInitialSize);

  std::string str1 = "hello";
  buf.append(str1);
  assert(buf.readable_bytes() == str1.size());
  assert(buf.writable_bytes() == Buffer::kInitialSize - str1.size());

  std::string str2 = buf.retrieve_as_string(str1.size());
  assert(str2 == str1);
  assert(buf.readable_bytes() == 0);
  assert(buf.writable_bytes() == Buffer::kInitialSize);

  std::cout << "  [PASS]" << std::endl;
}

void test_buffer_grow() {
  std::cout << "Test: Buffer Grow..." << std::endl;
  Buffer buf;
  std::string big_str(1200, 'x');
  buf.append(big_str);
  assert(buf.readable_bytes() == 1200);
  assert(buf.writable_bytes() > 0);
  assert(buf.writable_bytes() + 1200 >= Buffer::kInitialSize + 1200);

  std::string retrieved_str = buf.retrieve_as_string(1200);
  assert(retrieved_str == big_str);

  std::cout << "  [PASS]" << std::endl;
}

void test_buffer_makespace() {
  std::cout << "Test: Buffer MakeSpace..." << std::endl;
  Buffer buf;
  buf.retrieve_all();
  assert(buf.readable_bytes() == 0);
  assert(buf.prependable_bytes() == Buffer::kCheapPrepend);

  std::string str(200, 'x');
  buf.append(str);
  assert(buf.readable_bytes() == 200);

  buf.retrieve(100);
  assert(buf.readable_bytes() == 100);
  assert(buf.prependable_bytes() == Buffer::kCheapPrepend + 100);

  // 将追加的字符串大小改为900，以确保触发内存移动而不是重分配
  std::string big_str(900, 'y');
  buf.append(big_str);

  // 移动后，可读数据变为 100(旧) + 900(新) = 1000
  assert(buf.readable_bytes() == 1000);
  // 内存移动后，可丢弃空间应被重置
  assert(buf.prependable_bytes() == Buffer::kCheapPrepend);

  std::string final_str = buf.retrieve_as_string(1000);
  assert(final_str.size() == 1000);
  assert(final_str.substr(0, 100) == std::string(100, 'x'));
  assert(final_str.substr(100) == big_str);

  std::cout << "  [PASS]" << std::endl;
}

void test_find_crlf() {
  std::cout << "Test: Find CRLF..." << std::endl;
  Buffer buf;
  buf.append("hello\r\nworld");
  const char *crlf = buf.find_crlf();
  assert(crlf != nullptr);
  assert(crlf == buf.peek() + 5);

  buf.retrieve(7);
  assert(buf.find_crlf() == nullptr);

  std::cout << "  [PASS]" << std::endl;
}

void test_empty_and_special_chars() {
  std::cout << "Test: Empty and Special Chars..." << std::endl;
  Buffer buf;
  buf.append("");
  assert(buf.readable_bytes() == 0);

  std::string special_chars = "hello\0world\r\n";
  buf.append(special_chars);
  assert(buf.readable_bytes() == special_chars.size());
  assert(buf.retrieve_as_string(special_chars.size()) == special_chars);

  std::cout << "  [PASS]" << std::endl;
}

void test_retrieve_edge_cases() {
  std::cout << "Test: Retrieve Edge Cases..." << std::endl;
  Buffer buf;
  std::string s = "hello";
  buf.append(s);
  buf.retrieve(100); // Retrieve more than readable
  assert(buf.readable_bytes() == 0);

  std::cout << "  [PASS]" << std::endl;
}

void test_continuous_append_retrieve() {
  std::cout << "Test: Continuous Append and Retrieve..." << std::endl;
  Buffer buf;
  std::string full_data;
  for (int i = 0; i < 100; ++i) {
    std::string part = "data_part_" + std::to_string(i) + ";";
    buf.append(part);
    full_data += part;
  }
  assert(buf.readable_bytes() == full_data.size());
  assert(buf.retrieve_as_string(full_data.size()) == full_data);

  std::cout << "  [PASS]" << std::endl;
}

void test_read_fd() {
  std::cout << "Test: Read from FD..." << std::endl;
  int fds[2];
  assert(pipe(fds) == 0);

  Buffer buf;
  std::string data = "some data to be written to pipe";
  write(fds[1], data.c_str(), data.size());
  close(fds[1]); // Close write end

  int saved_errno = 0;
  ssize_t n = buf.read_fd(fds[0], &saved_errno);
  close(fds[0]); // Close read end

  assert(n == static_cast<ssize_t>(data.size()));
  assert(saved_errno == 0);
  assert(buf.readable_bytes() == data.size());
  assert(buf.retrieve_as_string(data.size()) == data);

  std::cout << "  [PASS]" << std::endl;
}

int main() {
  std::cout << "--- Starting Buffer Unit Tests ---" << std::endl;
  test_buffer_append_retrieve();
  test_buffer_grow();
  test_buffer_makespace();
  test_find_crlf();
  test_empty_and_special_chars();
  test_retrieve_edge_cases();
  test_continuous_append_retrieve();
  test_read_fd();
  std::cout << "\n✅ All Buffer tests passed!" << std::endl;
  return 0;
}