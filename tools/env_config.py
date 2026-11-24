#!/usr/bin/env python3
"""Load optional local environment overrides from <repo>/.env.local."""
import os
from pathlib import Path


def load_local_env(repo_root: Path | None = None) -> Path | None:
    if repo_root is None:
        repo_root = Path(__file__).resolve().parent.parent
    env_file = repo_root / ".env.local"
    if not env_file.is_file():
        return None
    for raw_line in env_file.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, val = line.split("=", 1)
        key = key.strip()
        val = val.strip()
        if len(val) >= 2 and ((val[0] == '"' and val[-1] == '"') or (val[0] == "'" and val[-1] == "'")):
            val = val[1:-1]
        if key and key not in os.environ:
            os.environ[key] = val
    return env_file
