#!/bin/bash
# 脚本：enter-dev.sh
# 功能：快速进入或启动并进入 mini-redis-dev 开发容器

CONTAINER_NAME="mini-redis-dev"

# 检查容器是否存在且正在运行
if [ "$(docker ps -q -f name=^/${CONTAINER_NAME}$)" ]; then
    echo "容器 '$CONTAINER_NAME' 正在运行，直接进入..."
# 检查容器是否存在但已停止
elif [ "$(docker ps -aq -f status=exited -f name=^/${CONTAINER_NAME}$)" ]; then
    echo "容器 '$CONTAINER_NAME' 已停止，正在启动并进入..."
    docker start "$CONTAINER_NAME" > /dev/null
# 如果容器不存在
else
    echo "错误：找不到名为 '$CONTAINER_NAME' 的容器。"
    echo "请先运行 'docker run ...' 命令来创建它。"
    exit 1
fi

#执行进入容器的命令
docker exec -it $CONTAINER_NAME /bin/bash