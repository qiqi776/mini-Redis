#!/bin/bash
# 脚本：build.sh
# 功能：清理、配置并构建整个项目
set -e # 如果任何命令失败，立即退出脚本
echo "开始清理并构建项目...."
# 我们总是从一个干净的状态开始
rm -rf build
# 将配置和构建合二为一
cmake -G "Ninja" -B build
cmake --build build

echo " 项目构建成功！可执行文件位于build/目录中。"