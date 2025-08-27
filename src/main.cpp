import application;
#include <iostream>
int main(int argc, char *argv[]) {
  // 创建应用程序实例
  Application app;
  // 获取配置文件路径
  std::string config_file;
  if (argc > 1) {
    config_file = argv[1];
  }
  // 初始化应用程序
  if (!app.init(config_file)) {
    std::cerr << "应用程序初始化失败！" << std::endl;
    return 1;
  }
  app.run();
  return 0;
}