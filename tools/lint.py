#!/usr/bin/env python3
"""
lint —— BoostKit Patch 仓统一 lint 工具

对齐:
  - DEP-3 (Debian Enhancement Proposal 3, patches 头格式规范)
    https://dep-team.pages.debian.net/deps/dep3/
  - Yocto/OpenEmbedded Upstream-Status 8 状态语义
    https://docs.yoctoproject.org/dev/dev-manual/common-tasks.html#patches
  - git format-patch 邮件式头(From/Date/Subject/Signed-off-by)
  - OpenWrt package/<name>/Config.in (bool + depends + default schema)
  - Linux kernel Kconfig (depends / select 语义)
  - Buildroot package/<name>/ (patch 按目录文件名字典序)

用法:
  python3 .github/lint.py headers <patch-or-dir>...    # patch 头校验(DEP-3 6 必填 + 条件必填)
  python3 .github/lint.py manifest <version-dir>...    # manifest.yaml 校验(schema + depends + DEP-3)
  python3 .github/lint.py all <version-dir>...         # 一键全量(headers + manifest)

退出码:
  0 全过 / 1 有失败
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

import yaml


DEP3_REQUIRED = ("Description", "Origin", "Upstream-Status", "Applies-To", "Maintainer", "Last-Update")

VALID_STATUSES = frozenset({
    "Pending", "Submitted", "Accepted", "Rejected",
    "Backport", "Inappropriate", "Denied", "Inactive-Upstream",
})

STATUS_REQUIRES_PR = {"Submitted", "Accepted", "Backport"}
STATUS_REQUIRES_COMMIT = {"Accepted", "Backport"}
STATUS_REQUIRES_WHITELIST_REASON = {
    "Rejected", "Inappropriate", "Denied", "Inactive-Upstream",
}
MIN_WHITELIST_REASON_LEN = 30
MIN_DESCRIPTION_LEN = 20

HEADER_END_RE = re.compile(r"^(diff --git |--- |\+\+\+ )", re.MULTILINE)
SHA_RE = re.compile(r"^[0-9a-f]{40}$")

# ─── manifest features.<name> 6 态治理字段（Yocto 总分形态） ───
VALID_MANIFEST_STATUSES = frozenset({
    "Pending",       # 已发上游 PR/邮件, 等回复
    "Submitted",     # 上游复核中
    "Backport",      # 从更高版本反向移植
    "Denied",        # 上游明确拒绝
    "Inappropriate", # 不适合上游（白名单）
    "Accepted",      # 上游已合并
})
EMAIL_RE = re.compile(r"^[^@\s]+@[^@\s]+\.[^@\s]+$")
DATE_RE = re.compile(r"^\d{4}-\d{2}-\d{2}$")

# status 状态转换合法性（防止 Inappropriate→Accepted 跳变）
STATUS_TRANSITIONS = {
    "Draft":        {"Pending", "Obsolete"},          # 仅迁移历史时用
    "Pending":      {"Submitted", "Backport", "Denied", "Inappropriate", "Obsolete"},
    "Submitted":    {"Pending", "Accepted", "Backport", "Denied", "Inappropriate", "Obsolete"},
    "Backport":     {"Accepted", "Obsolete"},
    "Denied":       {"Pending", "Obsolete"},          # 可重新发起
    "Inappropriate": {"Obsolete"},                    # 不可转为 Accepted
    "Accepted":     {"Obsolete"},
    "Obsolete":     set(),
}

# status → notes / upstream_metadata 字段联动
MANIFEST_STATUS_REQUIRES_NOTES = frozenset({
    "Inappropriate",  # 必填：不适合上游原因
    "Denied",         # 必填：上游拒绝原因
    "Backport",       # 必填：源头 commit 或新版本号
})
MANIFEST_STATUS_REQUIRES_COMMIT = frozenset({
    "Accepted",       # 必填：上游 commit hash
})
MANIFEST_STATUS_REQUIRES_PR = frozenset({
    "Pending",        # 必填：已发的 PR / 邮件列表链接
    "Submitted",
})
MIN_MANIFEST_NOTES_LEN = 10


def parse_header(text: str) -> tuple[dict[str, str], str]:
    headers: dict[str, str] = {}
    body_lines: list[str] = []
    in_header = True
    last_key: str | None = None

    lines = text.splitlines()
    i = 0
    while i < len(lines):
        line = lines[i]
        if in_header:
            m = HEADER_END_RE.match(line)
            if m:
                in_header = False
                body_lines.append(line)
                i += 1
                continue
            km = re.match(r"^([A-Za-z][A-Za-z0-9-]*):\s*(.*)$", line)
            if km:
                key = km.group(1)
                val = km.group(2).rstrip()
                if val == "|":
                    block: list[str] = []
                    i += 1
                    while i < len(lines):
                        cont = lines[i]
                        if cont.startswith("    ") or cont.startswith("\t"):
                            block.append(cont[4:] if cont.startswith("    ") else cont[1:])
                            i += 1
                        elif cont.strip() == "":
                            block.append("")
                            i += 1
                        else:
                            break
                    headers[key] = "\n".join(block).strip()
                    last_key = key
                    continue
                else:
                    headers[key] = val
                    last_key = key
                    i += 1
                    continue
            elif line.startswith((" ", "\t")) and last_key is not None:
                cont = line.lstrip(" \t")
                if headers[last_key]:
                    headers[last_key] = headers[last_key] + " " + cont
                else:
                    headers[last_key] = cont
                i += 1
                continue
            elif line.strip() == "":
                i += 1
                continue
            else:
                i += 1
                continue
        else:
            body_lines.append(line)
            i += 1

    return headers, "\n".join(body_lines)


def parse_header_minimal(text: str) -> dict[str, str]:
    headers: dict[str, str] = {}
    lines = text.splitlines()
    in_header = True
    for line in lines:
        if in_header:
            if HEADER_END_RE.match(line):
                break
            m = re.match(r"^([A-Za-z][A-Za-z0-9-]*):\s*(.*)$", line)
            if m:
                key = m.group(1)
                val = m.group(2).rstrip()
                headers[key] = "<multiline>" if val == "|" else val
            elif line.startswith((" ", "\t")) and headers:
                pass
    return headers


# === subcommand: headers ===

def lint_patch(patch_path: Path) -> list[str]:
    errs: list[str] = []
    text = patch_path.read_text(encoding="utf-8", errors="replace")

    if not HEADER_END_RE.search(text):
        return [f"{patch_path}: 不是 patch 文件(缺少 diff/---/+++ 段)"]

    headers, _ = parse_header(text)

    for f in DEP3_REQUIRED:
        if f not in headers or not headers[f].strip():
            errs.append(f"{patch_path}: 缺必填字段 {f}:")

    for f in ("From", "Subject", "Signed-off-by"):
        if f not in headers or not headers[f].strip():
            errs.append(f"{patch_path}: 缺必填字段 {f}:")

    desc = headers.get("Description", "").strip()
    if desc and len(desc) < MIN_DESCRIPTION_LEN:
        errs.append(
            f"{patch_path}: Description 太短 ({len(desc)} < {MIN_DESCRIPTION_LEN} 字符)"
        )

    lu = headers.get("Last-Update", "").strip()
    if lu and not re.match(r"^\d{4}-\d{2}-\d{2}$", lu):
        errs.append(
            f"{patch_path}: Last-Update={lu!r} 不是 YYYY-MM-DD 格式"
        )

    status = headers.get("Upstream-Status", "").strip()
    if status and status not in VALID_STATUSES:
        errs.append(
            f"{patch_path}: Upstream-Status={status!r} 非法;"
            f"允许: {', '.join(sorted(VALID_STATUSES))}"
        )

    if status in STATUS_REQUIRES_PR:
        if not headers.get("Upstream-PR", "").strip():
            errs.append(
                f"{patch_path}: Upstream-Status={status} → 必填 Upstream-PR:"
            )
    if status in STATUS_REQUIRES_COMMIT:
        commit = headers.get("Upstream-Commit", "").strip()
        if not commit:
            errs.append(
                f"{patch_path}: Upstream-Status={status} → 必填 Upstream-Commit:"
            )
        elif not SHA_RE.fullmatch(commit):
            errs.append(
                f"{patch_path}: Upstream-Commit={commit!r} 不是 40-char SHA"
            )
    if status in STATUS_REQUIRES_WHITELIST_REASON:
        reason = headers.get("Whitelist-Reason", "").strip()
        if len(reason) < MIN_WHITELIST_REASON_LEN:
            errs.append(
                f"{patch_path}: Upstream-Status={status} → Whitelist-Reason 必填且 ≥{MIN_WHITELIST_REASON_LEN} 字符"
                f"(当前 {len(reason)} 字符)"
            )

    return errs


def cmd_headers(paths: list[Path]) -> int:
    all_errs: list[str] = []
    for p in paths:
        errs = lint_patch(p)
        if errs:
            all_errs.extend(errs)
            for e in errs:
                print(f"  ✗ {e}", file=sys.stderr)
        else:
            print(f"  ✓ {p}")

    print(f"\n--- patch header lint: {len(paths)} 个文件, {len(all_errs)} 个错误 ---")
    return 0 if not all_errs else 1


# === subcommand: manifest ===

def lint_manifest(manifest_yaml: Path) -> list[str]:
    errs: list[str] = []
    version_dir = manifest_yaml.parent

    if not manifest_yaml.is_file():
        return [f"{manifest_yaml}: 不存在"]

    try:
        data = yaml.safe_load(manifest_yaml.read_text(encoding="utf-8"))
    except yaml.YAMLError as e:
        return [f"{manifest_yaml}: YAML 解析失败: {e}"]

    if not isinstance(data, dict):
        return [f"{manifest_yaml}: 顶层不是 dict"]

    for f in ("repo", "version", "commit"):
        if not data.get(f):
            errs.append(f"{manifest_yaml}: 缺 {f}:")

    commit = data.get("commit", "")
    if commit and not SHA_RE.fullmatch(commit):
        errs.append(f"{manifest_yaml}: commit 不是 40-char SHA: {commit!r}")

    # 发现 feature 目录
    feature_names: set[str] = set()
    all_patches: list[Path] = []
    for child in sorted(version_dir.iterdir()):
        if not child.is_dir() or child.name.startswith("."):
            continue
        pf = sorted(child.glob("*.patch"))
        if pf:
            feature_names.add(child.name)
            all_patches.extend(pf)

    # 可选 depends 校验
    depends = data.get("depends")
    if isinstance(depends, dict) and depends:
        for fname, deps in depends.items():
            if not isinstance(deps, list):
                errs.append(f"{manifest_yaml}: depends.{fname} 不是 list")
                continue
            if fname not in feature_names:
                errs.append(f"{manifest_yaml}: depends.{fname}: 引用了不存在的 feature")
            for d in deps:
                if d not in feature_names:
                    errs.append(f"{manifest_yaml}: depends.{fname}: 依赖的 {d!r} 目录不存在")

        # 环检测
        def has_cycle(start: str) -> bool:
            seen: set[str] = set()
            stack = [start]
            while stack:
                n = stack.pop()
                if n in seen:
                    continue
                seen.add(n)
                for d in depends.get(n, []):
                    if d == start:
                        return True
                    if d not in seen:
                        stack.append(d)
            return False

        for fname in depends:
            if has_cycle(fname):
                errs.append(f"{manifest_yaml}: depends: 检测到环依赖 (涉及 {fname!r})")

    # 孤儿 .patch
    for pf in sorted(version_dir.glob("*.patch")):
        errs.append(f"{pf}: 根目录 .patch (应放到 feature 子目录)")

    # DEP-3 必填
    for pf in sorted(version_dir.rglob("*.patch")):
        try:
            text = pf.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        hdr = parse_header_minimal(text)
        missing = [k for k in DEP3_REQUIRED if not hdr.get(k)]
        if missing:
            errs.append(f"{pf}: 缺 DEP-3 必填字段: {', '.join(missing)}")

    # install.deps 文件存在性 (warning)
    inst = data.get("install")
    if isinstance(inst, dict):
        for dep in (inst.get("deps", []) or []):
            dep_path = version_dir / dep
            if not dep_path.exists():
                errs.append(f"{manifest_yaml}: install.deps: {dep} 不存在")

    # ─── v6.0 总分形态：features.<name> 治理字段校验 ───
    features = data.get("features")
    if isinstance(features, dict):
        for fname, fg in features.items():
            if not isinstance(fg, dict):
                continue
            errs.extend(_lint_feature_governance(fname, fg, manifest_yaml))

        # 校验 patch header Upstream-Status 与 manifest status 一致性（数据防漂移）
        for fname in feature_names:
            fg = features.get(fname, {})
            if not isinstance(fg, dict):
                continue
            mf_status = fg.get("status", "").strip()
            for pf in sorted((version_dir / fname).glob("*.patch")):
                try:
                    text = pf.read_text(encoding="utf-8", errors="replace")
                except OSError:
                    continue
                hdr = parse_header_minimal(text)
                ph_status = hdr.get("Upstream-Status", "").strip()
                if ph_status and mf_status and ph_status != mf_status:
                    errs.append(
                        f"{manifest_yaml}: features.{fname}.status={mf_status!r} "
                        f"与 {pf}: Upstream-Status={ph_status!r} 不一致"
                    )

    return errs


def cmd_manifest(targets: list[Path]) -> int:
    all_errs: list[str] = []
    for my in targets:
        errs = lint_manifest(my)
        if errs:
            all_errs.extend(errs)
            for e in errs:
                print(f"  ✗ {e}", file=sys.stderr)
        else:
            print(f"  ✓ {my}: manifest OK, DEP-3 全")

    print(f"\n--- manifest: {len(targets)} 个, {len(all_errs)} 个错误 ---")
    return 0 if not all_errs else 1


# === manifest features.<name> 6 态治理字段校验（Yocto 总分形态） ===

def _lint_feature_governance(name: str, fg: dict, manifest_yaml: Path) -> list[str]:
    errs: list[str] = []

    def _s(v) -> str:
        """Normalize value to stripped string (PyYAML parses YYYY-MM-DD as date)."""
        if v is None:
            return ""
        return str(v).strip()

    # ── owner: email 格式 ──
    owner = _s(fg.get("owner"))
    if not owner:
        errs.append(f"{manifest_yaml}: features.{name}: 缺 owner 字段")
    elif not EMAIL_RE.match(owner):
        errs.append(f"{manifest_yaml}: features.{name}: owner={owner!r} 不是 email 格式")

    # ── date: YYYY-MM-DD 或 unknown ──
    date = _s(fg.get("date"))
    if not date:
        errs.append(f"{manifest_yaml}: features.{name}: 缺 date 字段 (YYYY-MM-DD 或 'unknown')")
    elif date != "unknown" and not DATE_RE.match(date):
        errs.append(f"{manifest_yaml}: features.{name}: date={date!r} 不是 YYYY-MM-DD")

    # ── status: 6 选 1 ──
    status = _s(fg.get("status"))
    if not status:
        errs.append(f"{manifest_yaml}: features.{name}: 缺 status 字段 (Yocto 6 态)")
    elif status not in VALID_MANIFEST_STATUSES:
        errs.append(
            f"{manifest_yaml}: features.{name}: status={status!r} 非法;"
            f"允许: {', '.join(sorted(VALID_MANIFEST_STATUSES))}"
        )

    # ── notes: 必填/选填依 status 而定 ──
    notes = _s(fg.get("notes"))
    if status in MANIFEST_STATUS_REQUIRES_NOTES and len(notes) < MIN_MANIFEST_NOTES_LEN:
        errs.append(
            f"{manifest_yaml}: features.{name}: status={status} → notes 必填且 ≥{MIN_MANIFEST_NOTES_LEN} 字符"
            f"(当前 {len(notes)} 字符)"
        )

    # ── upstream_commit / upstream_pr: 联动必填 ──
    upstream_commit = _s(fg.get("upstream_commit"))
    upstream_pr = _s(fg.get("upstream_pr"))

    if status in MANIFEST_STATUS_REQUIRES_COMMIT:
        if not upstream_commit:
            errs.append(f"{manifest_yaml}: features.{name}: status={status} → upstream_commit 必填")
        elif not SHA_RE.fullmatch(upstream_commit):
            errs.append(
                f"{manifest_yaml}: features.{name}: upstream_commit={upstream_commit!r} 不是 40-char SHA"
            )

    if status in MANIFEST_STATUS_REQUIRES_PR:
        if not upstream_pr:
            errs.append(f"{manifest_yaml}: features.{name}: status={status} → upstream_pr 必填")
        elif not (upstream_pr.startswith("http://") or upstream_pr.startswith("https://") or upstream_pr.startswith("git://")):
            errs.append(f"{manifest_yaml}: features.{name}: upstream_pr={upstream_pr!r} 不是 URL")

    return errs


# === subcommand: all ===

def cmd_all(manifest_targets: list[Path], original_args: list[str]) -> int:
    ret = 0
    if cmd_manifest(manifest_targets) != 0:
        ret = 1

    version_dirs: list[Path] = []
    for arg in original_args:
        p = Path(arg)
        if p.is_dir():
            version_dirs.append(p)

    if version_dirs:
        patch_paths: list[Path] = []
        for d in version_dirs:
            patch_paths.extend(sorted(d.rglob("*.patch")))
        if patch_paths and cmd_headers(patch_paths) != 0:
            ret = 1

    return ret


# === arg helpers ===

def collect_patch_paths(args: list[str]) -> list[Path]:
    paths: list[Path] = []
    for arg in args:
        p = Path(arg)
        if p.is_dir():
            paths.extend(sorted(p.rglob("*.patch")))
        elif p.is_file():
            paths.append(p)
        else:
            print(f"✗ {arg}: 不存在", file=sys.stderr)
            sys.exit(1)
    return paths


def collect_manifest_targets(args: list[str]) -> list[Path]:
    targets: list[Path] = []
    for arg in args:
        p = Path(arg)
        if p.is_dir():
            my = p / "manifest.yaml"
            if not my.is_file():
                print(f"  ⚠ {p}/manifest.yaml 不存在, 跳过", file=sys.stderr)
                continue
            targets.append(my)
        elif p.is_file() and p.name == "manifest.yaml":
            targets.append(p)
        else:
            print(f"✗ {arg}: 不是 manifest.yaml 也不是版本目录", file=sys.stderr)
            sys.exit(1)
    if not targets:
        print("✗ 未找到任何 manifest.yaml", file=sys.stderr)
        sys.exit(1)
    return targets


USAGE = """\
用法:
  python3 .github/lint.py headers <patch-or-dir>...
  python3 .github/lint.py manifest <version-dir-or-manifest.yaml>...
  python3 .github/lint.py all <version-dir>...

示例:
  python3 .github/lint.py headers versions/*/
  python3 .github/lint.py manifest versions/*/
  python3 .github/lint.py all versions/*/"""


def main(argv: list[str]) -> int:
    if len(argv) < 3:
        print(USAGE, file=sys.stderr)
        return 2

    subcmd = argv[1]
    rest = argv[2:]

    if subcmd == "headers":
        paths = collect_patch_paths(rest)
        if not paths:
            print("(无 .patch 文件)")
            return 0
        return cmd_headers(paths)
    elif subcmd == "manifest":
        targets = collect_manifest_targets(rest)
        return cmd_manifest(targets)
    elif subcmd == "all":
        targets = collect_manifest_targets(rest)
        return cmd_all(targets, rest)
    else:
        print(f"✗ 未知子命令: {subcmd}\n\n{USAGE}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
