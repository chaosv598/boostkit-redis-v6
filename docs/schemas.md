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

## 2. manifest.yaml

### 必填字段

| 字段 | 类型 | 语义 |
|------|------|------|
| `repo` | URL | 上游 git URL |
| `version` | string | upstream tag/version |
| `commit` | 40-char SHA | immutable pin |

### 可选字段

| 字段 | 类型 | 语义 |
|------|------|------|
| `depends` | dict | feature 间依赖（列表顺序 = 依赖项的 apply 顺序） |

### 模板

```yaml
repo: https://github.com/redis/redis
version: 7.0.15
commit: f35f36a265403c07b119830aa4bb3b7d71653ec9

# depends (可选):
#   C: [B, A]  → B 先于 A，都在 C 之前
```

## 3. 校验矩阵

| 校验项 | 命令 |
|--------|------|
| patch 头 6 必填 + 条件必填 | `python3 .github/lint.py headers versions/*/` |
| manifest schema + depends + DEP-3 | `python3 .github/lint.py manifest versions/*/` |
| 结构 + clean apply | `bash tools/verify.sh` |
