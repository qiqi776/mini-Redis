# 基础镜像
FROM ubuntu:24.04

# 设置非交互式安装，避免弹窗
ENV DEBIAN_FRONTEND=noninteractive

# 安装所有依赖：Clang, Git，和最新的 CMake
# 我们需要 git, wget, gnupg, ca-certificates, 和 software-properties-common
# 其中 software-properties-common 提供了 add-apt-repository，是 LLVM 脚本所需要的
RUN apt-get update && apt-get install -y \
    wget \
    software-properties-common \
    git \
    ca-certificates \
    gnupg

# 1. 安装 Clang 20（来自 LLVM 官方脚本）
# 这个脚本会自动添加 LLVM 的 APT 源并安装 Clang
RUN wget https://apt.llvm.org/llvm.sh && \
    chmod +x llvm.sh && \
    ./llvm.sh 20 && \
    rm llvm.sh

# 2. 【最终方案】手动、显式地添加 Kitware APT 源
#
# 这是最可靠的方法，它避免了官方脚本在非交互式环境中可能出现的各种问题。
#
# a. 添加 Kitware 的 GPG 密钥，用于验证软件包的真实性
RUN wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc | gpg --dearmor - > /usr/share/keyrings/kitware-archive-keyring.gpg

# b. 添加 Kitware 的 APT 源列表。我们必须确保它与基础镜像 ubuntu:24.04 的 'noble' 版本匹配。
RUN echo 'deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] https://apt.kitware.com/ubuntu/ noble main' > /etc/apt/sources.list.d/kitware.list

# 3. 从所有源中安装我们的最终工具链
# 在这一步，apt 会优先从 Kitware 的源中拉取最新版本的 cmake 和 ninja-build
RUN apt-get update && apt-get install -y \
    clang-20 \
    libc++-20-dev \
    libc++abi-20-dev \
    cmake \
    ninja-build

# ---环境设置与清理---
RUN rm -rf /var/lib/apt/lists/*

# 设置环境变量
ENV CC=/usr/bin/clang-20
ENV CXX=/usr/bin/clang++-20

# 工作目录和默认命令保持不变
WORKDIR /app
CMD ["tail", "-f", "/dev/null"]