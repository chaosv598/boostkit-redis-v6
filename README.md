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
│   ├── verify.sh            # 一键验证
│   └── lint.py              # patch 头 + manifest 校验
├── docs/
│   ├── schemas.md           # 治理：字段定义 + 校验矩阵
│   ├── zh/                  # 产品文档：特性指南 + 版本说明书
│   └── en/                  # 同上英文版
```

## 安装

repo/version/commit 已写在 `manifest.yaml` 中。

```bash
# 全部 feature
bash tools/apply_patch.sh src/Redis-7.0.15 /tmp/build

# 只选 rdb-aof-fallback
bash tools/apply_patch.sh --features "rdb-aof-fallback" src/Redis-7.0.15 /tmp/build
```

产品文档（特性使用指南、版本配套说明）见 `docs/zh/` / `docs/en/`。

## 本地验证

```bash
bash tools/verify.sh
```

## 设计参考

- [docs/schemas.md](docs/schemas.md) — YAML/header 字段权威定义
- 设计理念：Buildroot `.mk`（目录即配置）+ Kconfig `depends`（DFS 依赖解析）+ DEP-3（patch 头规范）

## 许可证

- 补丁: Apache 2.0（[LICENSE](LICENSE)）
- 上游 Redis: BSD-3-Clause（各 patch 头部保留）
