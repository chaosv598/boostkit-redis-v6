# Redis Network Optimization

## Brand

Kunpeng BoostKit Redis

## About

Kunpeng ARM platform optimization patches for Redis, based on the KRAIO (Kunpeng Redis Asynchronous I/O) engine.
Covers Redis 4.0.14 / 6.0.15 / 6.0.20 / 7.0.15.

## Layout

```
boostkit-redis-v6/
├── src/
│   ├── Redis-7.0.15/
│   │   ├── manifest.yaml    # ★ version pin + install steps (Buildroot .mk style)
│   │   ├── kunpeng-hw-accel/
│   │   ├── jemalloc-arm64/
│   │   ├── rdb-aof-fallback/
│   │   └── rpm_build/
│   └── ... (3 more versions)
├── tools/
│   ├── apply_patch.sh       # Buildroot-style patch applier
│   └── verify.sh            # one-shot verification
├── .github/
│   ├── lint.py              # patch header + manifest validator
│   └── workflows/ci.yml
├── docs/
│   ├── schemas.md           # governance: field definitions
│   ├── zh/                  # product docs: feature guides + release notes
│   └── en/
```

## Install

repo/version/commit live in `manifest.yaml`.

```bash
# All features
bash tools/apply_patch.sh src/Redis-7.0.15 /tmp/build

# Feature subset
bash tools/apply_patch.sh --features "rdb-aof-fallback" src/Redis-7.0.15 /tmp/build

Product documentation (feature guides, compatibility) in `docs/zh/` / `docs/en/`.

## Verify

```bash
bash tools/verify.sh
python3 .github/lint.py manifest src/*/
python3 .github/lint.py headers src/*/
```

## Design

- [docs/schemas.md](docs/schemas.md) — authoritative schema
- Philosophy: Buildroot (directory-as-config) + Kconfig depends (DFS) + DEP-3 (patch headers)

## License

- Patches: Apache 2.0 ([LICENSE](LICENSE))
- Upstream Redis: BSD-3-Clause (per patch header)
