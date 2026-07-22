#!/bin/bash
# Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
# this script is licensed under the Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#    http://license.coscl.org.cn/MulanPSL2
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
# PURPOSE.
# See the Mulan PSL v2 for more details.
# Description: Automatically build a standardized rpmbuild directory structure. Simultaneously, prepare the necessary
# community source code and optimization patches for RPM package construction.Ensure external network access for the server.

RPMBUILD_DIR=~/rpmbuild
SOURCES=$RPMBUILD_DIR/SOURCES

mkdir -p $SOURCES

echo "📥 下载 Redis 源码..."
wget -O $SOURCES/redis-7.0.15.tar.gz http://download.redis.io/releases/redis-7.0.15.tar.gz

echo "📥 下载 liburing 打包元数据..."
git clone https://gitee.com/src-openeuler/liburing.git
cd liburing
# 提取真正的源码包
tar -xzf liburing-2.4.tar.gz
# 将嵌套的源码包复制到 SOURCES
cp liburing-2.4.tar.gz $SOURCES/
cd ..
rm -rf liburing


echo "📥 克隆 libconfig 仓库..."
git clone https://gitee.com/src-openeuler/libconfig.git
cd libconfig

# 检查 v1.8.1.tar.gz 是否存在
if [ -f "v1.8.1.tar.gz" ]; then
    echo "📦 复制 v1.8.1.tar.gz 到 $SOURCES ..."
    cp v1.8.1.tar.gz "$SOURCES/libconfig-1.8.1.tar.gz"
    echo "✅ 成功复制: $SOURCES/libconfig-1.8.1.tar.gz"
else
    echo "❌ 错误：v1.8.1.tar.gz 不存在！"
    ls -l  # 查看当前目录内容，用于调试
    exit 1
fi

# 返回并清理
cd ..
rm -rf libconfig

echo "📥 下载 kraio..."
git clone https://gitcode.com/BoostKit/kraio.git
cd kraio
git archive --format=tar --prefix=kraio/ --output=$SOURCES/kraio.tar HEAD
gzip $SOURCES/kraio.tar
cd ..
rm -rf kraio

git clone https://gitcode.com/BoostKit/Redis.git
cd Redis

if [ -f "redis-7.0.15-adapt-iouring.patch" ]; then
    cp redis-7.0.15-adapt-iouring.patch $SOURCES/
else
    echo " 错误：未找到 redis-7.0.15-adapt-iouring.patch"
    exit 1
fi
cd ..
rm -rf Redis

echo "✅ 所有源码和补丁已下载并放入 $SOURCES"