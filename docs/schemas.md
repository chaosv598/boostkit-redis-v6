# Schema 权威定义 (v6.0 · 总分形态)

> v6.1 起：manifest 是 **总 + 分** 形态（Yocto Upstream-Status 借鉴）
> - **总**：顶层 repo/version/commit（上游身份基线）
> - **分**：每个 patch 在 `features.<name>` 内扩展 4 个治理字段
>   - `owner`（作者邮箱）
>   - `date`（commit 日期 YYYY-MM-DD 或 `unknown`）
>   - `status`（Yocto Upstream-Status 6 态）
>   - `notes`（PR 链接 / 不适合上游原因 / Backport 源头 commit 等）
> - 同时支持 `upstream_commit` / `upstream_pr` 联动字段（status 决定必填/选填）

## 目录结构

```
<repo>/
├── .github/
│   ├── lint.py                 # patch 头 + manifest 校验（v6.0）
│   └── workflows/ci.yml        # CI（4 步）
├── tools/
│   ├── apply_patch.sh          # Buildroot 风格 patch 应用器
│   ├── lint.py                 # 统一 lint（双权限: DEP-3 头 + manifest）
│   └── verify.sh               # 一键验证（lint → apply → install → build → status 报表）
├── docs/
│   ├── schemas.md              # 本文档
│   ├── zh/                     # 产品文档：特性指南 + 版本说明书
│   └── en/
└── src/
    └── <Upstream-Version>/     # 例: Redis-7.0.15
        ├── manifest.yaml       # ★ 唯一配置文件（总 + 分）
        ├── <feature>/          # feature = 含 .patch 的子目录
        │   └── *.patch         # DEP-3 邮件式头（保留冗余，与 manifest 防漂移）
        ├── deps/               # 构建依赖（可选）
        └── rpm_build/          # RPM 打包（可选）
```

## 1. 总：manifest 顶层（上游身份）

| 字段 | 类型 | 必填 | 语义 |
|------|------|:--:|------|
| `repo` | URL | 是 | 上游 git URL |
| `version` | string | 是 | upstream tag/version |
| `commit` | 40-char SHA | 是 | immutable pin |
| `install` | dict | 否 | 编译步骤（见 §4） |

## 2. 分：manifest features.<name>（Yocto Upstream-Status 6 态）

### 2.1 治理字段（每 patch 必填/选填矩阵）

| 字段 | 类型 | 必填 | 联动 status |
|------|------|:--:|------|
| `owner` | email | 是 | — |
| `date` | `YYYY-MM-DD` \| `unknown` | 是 | — |
| `status` | 6 态 enum | 是 | — |
| `notes` | string ≥10 | 条件 | Inappropriate/Denied/Backport 必填 |
| `upstream_commit` | 40-char SHA | 条件 | Accepted 必填 |
| `upstream_pr` | URL | 条件 | Pending/Submitted 必填 |
| `depends` | list[str] | 否 | — |
| `conflicts` | list[str] | 否 | — |

### 2.2 status 6 态（Yocto Upstream-Status 标准）

| status | 语义 | manifest 联动必填 | patch header 协同字段 |
|--------|------|-----------------|---------------------|
| `Pending` | 已发上游 PR/邮件，等回复 | `upstream_pr` | `Upstream-Status: Pending` |
| `Submitted` | 上游复核中 | `upstream_pr` | `Upstream-Status: Submitted` |
| `Backport` | 从更高版本反向移植 | `notes`（源头 commit / 新版本号） | `Upstream-Status: Backport` |
| `Denied` | 上游明确拒绝 | `notes`（拒绝原因） | `Upstream-Status: Denied` |
| `Inappropriate` | **不适合上游**（白名单） | `notes`（不适合原因） | `Upstream-Status: Inappropriate` + `Whitelist-Reason` |
| `Accepted` | 上游已合并 | `upstream_commit` | `Upstream-Status: Accepted` + `Upstream-Commit` |

### 2.3 状态转换合法性

```
Draft ─▶ Pending ─▶ Submitted ─▶ Accepted ─▶ Obsolete
            │           │             ▲
            ▼           ▼             │
        Backport / Denied / ──────── Accepted (Backport 反向转 Accepted)
            Inappropriate
```

lint.py 内置 `STATUS_TRANSITIONS` 表，CI 自动拒绝以下跳变：
- `Inappropriate → Accepted`（已声明不适合上游，不能又"已合入"）
- `Pending → Backport`（未发就反移植，逻辑不通）
- `Accepted → Pending`（已合入不能撤回）

### 2.4 数据防漂移

manifest `features.<name>.status` 与 patch header `Upstream-Status:` 必须**完全一致**——任意修改两边要同步。lint.py 自动比对并报：
```
✗ src/Redis-7.0.15/manifest.yaml: features.kunpeng-dtoe.status='Inappropriate'
  与 src/Redis-7.0.15/kunpeng-dtoe/0001-adapt-dtoe.patch: Upstream-Status='Pending' 不一致
```

## 3. Patch 邮件式头（DEP-3，作为冗余与变更追溯）

> **业务优先**：manifest 是单一权威，patch 头作为"变更追溯 + 离线 grep 友好"冗余备份。

| 字段 | 类型 | 必填 | 语义 |
|------|------|:--:|------|
| `From` | `Name <email>` | 是 | 作者 |
| `Subject` | string | 是 | 标题 |
| `Description` | string ≥20 | 是 | 改了什么 + 为何改 |
| `Origin` | URL/string | 是 | 出处 |
| `Upstream-Status` | enum | 是 | **必须与 manifest features.X.status 完全一致** |
| `Applies-To` | string | 是 | 适用上游版本 |
| `Maintainer` | `Name <email>` | 是 | 维护人（冗余：manifest.features.X.owner 是权威） |
| `Last-Update` | `YYYY-MM-DD` | 是 | 最后更新日期（冗余：manifest.features.X.date 是权威） |
| `Signed-off-by` | `Name <email>` | 是 | DCO 签名 |
| `Upstream-PR` | URL | 条件 | 与 manifest.upstream_pr 同步 |
| `Upstream-Commit` | 40-char SHA | 条件 | 与 manifest.upstream_commit 同步 |
| `Whitelist-Reason` | string ≥30 | 条件 | 与 manifest.notes 同步（Inappropriate/Denied 状态） |

## 4. manifest install（可选）

声明编译步骤，`apply_patch.sh` 在 apply 后自动执行。

| 字段 | 类型 | 语义 |
|------|------|------|
| `install.deps` | list[str] | build 依赖文件清单（相对版本目录） |
| `install.configure` | string | configure 命令 |
| `install.build` | string | build 命令 |

## 5. 完整模板

```yaml
# 使用: bash tools/apply_patch.sh src/Redis-7.0.15 /tmp/build
#       bash tools/verify.sh status  → patch 状态分布报表

repo: https://github.com/redis/redis
version: 7.0.15
commit: f35f36a265403c07b119830aa4bb3b7d71653ec9

features:
  kunpeng-dtoe:
    owner: twwang1@qq.com
    date: 2026-03-16
    status: Inappropriate
    notes: "Kunpeng ARM DTOE DMA 网络路径, 依赖硬件特定驱动, 上游 Redis 不接受"
    depends: []
    conflicts: [kunpeng-iouring]

  rdb-aof-fallback:
    owner: twwang1@qq.com
    date: 2026-07-13
    status: Pending
    notes: "grisu2 d2string + addReplyDouble 性能修复, 已发 redis-dev 邮件列表"
    upstream_pr: https://gist.github.com/twwang1/abcdef123456
    depends: []

install:
  configure: make distclean
  build: make -j$(nproc)
```

## 6. 校验矩阵

| 校验项 | 命令 | 退出码 |
|--------|------|:------:|
| patch 头 + manifest 字段一致性 | `python3 tools/lint.py all src/*/` | 0/1 |
| 仅 manifest 字段（v6.1 重点） | `python3 tools/lint.py manifest src/*/` | 0/1 |
| 仅 patch 头 DEP-3 | `python3 tools/lint.py headers src/*/` | 0/1 |
| 一键验证 + 状态分布报表 | `bash tools/verify.sh all` | 0/1 |
| 仅状态分布（不 apply/build） | `bash tools/verify.sh status` | 0/1 |

## 7. 业界出处映射

| 维度 | v6.0 总分形态 | 业界出处 |
|------|--------------|---------|
| manifest 顶层（上游身份） | `repo/version/commit` + `install` | **Yocto** recipe (SUMMARY/LICENSE/SRC_URI) |
| manifest 分项（patch 元数据） | `features.X.{owner,date,status,notes}` | **Yocto** Upstream-Status + OpenWrt Config.in |
| status 6 态 | Pending/Submitted/Backport/Denied/Inappropriate/Accepted | **Yocto** `dev-manual/common-tasks.html#patches` |
| 数据防漂移 | manifest features.X.status == patch Upstream-Status | **DEP-3** + 内部约定 |
| patch 头规范 | 6 必填 + 条件必填 | **DEP-3** (Debian) — 保留作离线 grep + 变更追溯 |
| 依赖解析 | depends DFS + 环检测 | **Linux Kconfig** `depends on` (scripts/kconfig/symbol.c) |
| 冲突检查 | conflicts 集合 | **Kconfig** `depends on !` / **Debian** `Conflicts:` |
| 配置载体 | manifest.yaml（一个文件） | **Buildroot** `.mk` / **OpenWrt** `Makefile` |
| feature 子集 | `--features` CLI | **OpenWrt** `CONFIG_FOO=y` / **Yocto** `DISTRO_FEATURES` |
| 编译验证 | install: configure/build | **Buildroot** `BUILD_CMDS` / **RPM** `%build` |
| 文档分层 | README → manifest → schemas → docs/zh/en | **Linux kernel** `Kconfig` + `Documentation/` |

## 8. 版本沿革

| 版本 | 形态 | 说明 |
|------|------|------|
| v6.0 早 | DEP-3 only | patch 头 9 字段，业务诉求是"patch 不改" |
| v6.0 早备选 | manifest-only | 仅 manifest，无 patch 头（业务弃选） |
| v6.0 末（当前 master） | **总分形态** | manifest features.X 新增 4 治理字段，patch 头冗余保留 |
| v6.0 备选分支 | `v6.0-patchheader-status` | manifest 不带 status，全靠 patch 头 Upstream-Status |
