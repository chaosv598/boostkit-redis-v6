# Schema 权威定义 (v6.0)

## 目录结构

```
boostkit-redis-v6/
├── .github/
│   ├── lint.py                 # patch 头 + manifest 校验
│   └── workflows/ci.yml        # CI（3 步）
├── tools/
│   ├── apply_patch.sh          # Buildroot 风格 patch 应用器
│   └── verify.sh               # 一键验证
├── docs/
│   └── schemas.md              # 本文档
└── src/
    └── <Upstream-Version>/     # 例: Redis-7.0.15
        ├── manifest.yaml       # 上游 pin + 可选 depends
        └── <feature>/          # feature 目录（含 .patch 的子目录）
            └── *.patch         # DEP-3 邮件式头（6 必填）+ diff
```

## 1. Patch 邮件式头（DEP-3）

### 必填字段

| 字段 | 类型 | 语义 |
|------|------|------|
| `From` | `Name <email>` | 作者 |
| `Subject` | string | 标题 |
| `Description` | string ≥20 字符 | 改了什么 + 为何改 |
| `Origin` | URL / string | 出处 |
| `Upstream-Status` | enum | 上游合入状态 |
| `Applies-To` | string | 适用上游版本 |
| `Maintainer` | `Name <email>` | 维护人 |
| `Last-Update` | `YYYY-MM-DD` | 最后更新日期 |
| `Signed-off-by` | `Name <email>` | DCO 签名 |

### 条件必填

| 字段 | 触发条件 |
|------|----------|
| `Upstream-PR` | Submitted / Accepted / Backport |
| `Upstream-Commit` | Accepted / Backport |
| `Whitelist-Reason` ≥30字符 | Rejected / Inappropriate / Denied / Inactive-Upstream |

### Upstream-Status 枚举

```
Pending / Submitted / Accepted / Rejected
Backport / Inappropriate / Denied / Inactive-Upstream
```

## 2. manifest.yaml（Buildroot `.mk` / OpenWrt `Makefile` / RPM `.spec` 风格）

一个文件 = 版本 pin + 构建安装说明。YAML 字段给脚本读，注释给人读。

### 字段

| 字段 | 必填 | 类型 | 语义 |
|------|------|------|------|
| `repo` | 是 | URL | 上游 git URL |
| `version` | 是 | string | upstream tag/version |
| `commit` | 是 | 40-char SHA | immutable pin |
| `depends` | 否 | dict | feature 间依赖（见下） |

### depends（可选）

```yaml
depends:
  C: [B, A]  # C 依赖 B 和 A，B 先于 A apply
```

### install（可选，Buildroot `.mk` 风格）

声明 build/install 步骤，`apply_patch.sh` apply 完成后自动提示：

| 字段 | 类型 | 语义 |
|------|------|------|
| `install.deps` | list[str] | build 依赖文件清单（相对版本目录） |
| `install.configure` | string | configure 命令 |
| `install.build` | string | build 命令 |

```yaml
install:
  configure: make distclean
  build: make -j$(nproc) USE_KRAIO=1
  deps:
    - rpm_build/include/kraio.h
    - rpm_build/lib/libkraio.so
```

不声明 `install` = 纯 patch overlay，apply 即结束。

### 注释规范

YAML 注释 = 给人读的文档，约定 # 后必备三段：

```yaml
# Feature: <描述每个含 .patch 的子目录>
# Build 依赖: <deps/, rpm_build/ 等非 patch 目录说明>
# 安装: <clone + apply + build 完整步骤>
```

业界出处：

| 方案 | 文件 | 模式 |
|------|------|------|
| **Buildroot** | `package/redis/redis.mk` | `REDIS_VERSION` + 构建命令，同一文件 |
| **OpenWrt** | `package/dnsmasq/Makefile` | `PKG_VERSION` + `define Build/Compile` |
| **RPM** | `redis.spec` | `Version:` + `%build` script |

## 3. 校验矩阵

| 校验项 | 命令 |
|--------|------|
| patch 头 6 必填 + 条件必填 | `python3 .github/lint.py headers versions/*/` |
| manifest schema + depends + DEP-3 | `python3 .github/lint.py manifest versions/*/` |
| 结构 + clean apply | `bash tools/verify.sh` |
