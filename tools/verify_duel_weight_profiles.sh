#!/usr/bin/env bash
set -euo pipefail

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$project_root"

if ! command -v sha256sum >/dev/null 2>&1; then
  echo "sha256sum is required to verify generated duel weight profiles" >&2
  exit 1
fi

sha256sum --check --strict configs/evaluation_weights/duel_weight_profiles.sha256
