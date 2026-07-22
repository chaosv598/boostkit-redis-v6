# Redis 网络优化特性

## 项目品牌名称

Kunpeng BoostKit Redis

## 项目介绍

本仓库提供 Redis 的 Kunpeng ARM 平台优化补丁，核心能力是 KRAIO（Kunpeng Redis Asynchronous I/O）方案。
覆盖 Redis 4.0.14 / 6.0.15 / 6.0.20 / 7.0.15 四个版本。

## 目录结构

```
boostkit-redis-v6/
├── src/
│   ├── Redis-4.0.14/
│   │   ├── manifest.yaml    # ★ 版本 pin + 安装说明（Buildroot .mk 风格）
│   │   ├── kbaio/           # patch: io_uring 异步 I/O 内核模块
│   │   └── deps/            # build 依赖: kbaio-src 源码
│   ├── Redis-6.0.15/
│   │   ├── manifest.yaml
│   │   └── kunpeng-hw-accel/
│   ├── Redis-6.0.20/
│   │   ├── manifest.yaml
│   │   ├── kunpeng-hw-accel/
│   │   ├── kbaio/
│   │   └── deps/
│   └── Redis-7.0.15/
│       ├── manifest.yaml
│       ├── kunpeng-hw-accel/
│       ├── jemalloc-arm64/
│       ├── rdb-aof-fallback/
│       └── rpm_build/       # RPM spec + Kraio SDK
├── tools/
│   ├── apply_patch.sh       # Buildroot 风格 patch 应用器
│   └── verify.sh            # 一键验证
├── .github/
│   ├── lint.py              # patch 头 + manifest 校验
│   └── workflows/ci.yml     # CI（3 步）
└── docs/
    └── schemas.md           # 字段权威定义 + 校验矩阵
```

## 安装

每个版本的安装步骤写在对应 `manifest.yaml` 注释中，直接看对应文件即可。

```bash
# 以 Redis-7.0.15 为例，manifest.yaml 注释里写了完整步骤:
#   cat src/Redis-7.0.15/manifest.yaml

# 一键 apply:
bash tools/apply_patch.sh https://github.com/redis/redis \
    f35f36a265403c07b119830aa4bb3b7d71653ec9 \
    src/Redis-7.0.15 /tmp/build

# 子集选择（只打 rdb-aof-fallback）:
ACTIVE_FEATURES="rdb-aof-fallback" bash tools/apply_patch.sh ...
```

## 本地验证

```bash
bash tools/verify.sh                                # 结构 + clean apply
python3 .github/lint.py manifest src/*/              # manifest schema + DEP-3
python3 .github/lint.py headers src/*/               # patch 头 schema
```

## 设计参考

- [docs/schemas.md](docs/schemas.md) — YAML/header 字段权威定义
- 设计理念：Buildroot `.mk`（目录即配置）+ Kconfig `depends`（DFS 依赖解析）+ DEP-3（patch 头规范）

## 许可证

- 补丁: Apache 2.0（[LICENSE](LICENSE)）
- 上游 Redis: BSD-3-Clause（各 patch 头部保留）
