#!/bin/bash
# 脚本:run.sh
# 功能：运行一个指定的目标程序
set -e
TARGET=$1
#检查是否提供了目标名
if [ -z "$TARGET" ]; then
    echo " 错误：请指定要运行的程序(例如: ./run.sh server)"
    exit 1
fi
EXECUTABLE_PATH="./build/$TARGET"
#检查程序是否存在
if [ ! -f "$EXECUTABLE_PATH" ]; then
    echo " 程序'$TARGET'不存在于build/目录中。"
    echo " 请先运行./build.sh脚本来编译项目。"
    exit 1
fi
echo " 准备运行：$TARGET"
echo "-----------------"
$EXECUTABLE_PATH