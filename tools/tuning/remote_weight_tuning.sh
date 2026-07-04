#!/usr/bin/env bash
set -euo pipefail

REMOTE="${REMOTE:-scv@192.168.1.6}"
REMOTE_DIR="${REMOTE_DIR:-/tmp/playing-battlesnake-weight-tuning}"
SESSION="${SESSION:-battlesnake-weight-tuning}"
TRIALS="${TRIALS:-200}"
FIXED_DEPTH="${FIXED_DEPTH:-3}"
TIME_BUDGET_MS="${TIME_BUDGET_MS:-5000}"
LIMIT="${LIMIT:-}"
REMOTE_PYTHON="${REMOTE_DIR}/.venv/bin/python"

require_positive_int() {
  local name="$1"
  local value="$2"
  if [[ ! "${value}" =~ ^[1-9][0-9]*$ ]]; then
    echo "${name} must be a positive integer" >&2
    exit 2
  fi
}

require_positive_int TRIALS "${TRIALS}"
require_positive_int FIXED_DEPTH "${FIXED_DEPTH}"
require_positive_int TIME_BUDGET_MS "${TIME_BUDGET_MS}"
if [[ -n "${LIMIT}" ]]; then
  require_positive_int LIMIT "${LIMIT}"
fi

mkdir -p artifacts/weight_tuning

rsync -az --delete \
  --exclude '.git/' \
  --exclude '__pycache__/' \
  --include 'battlesnake/battlesnake_native*.so' \
  --exclude '*.so' \
  --exclude 'build/' \
  --exclude '.venv/' \
  --exclude 'artifacts/weight_tuning/' \
  ./ "${REMOTE}:${REMOTE_DIR}/"

LIMIT_ARGS=()
if [[ -n "${LIMIT}" ]]; then
  LIMIT_ARGS=(--limit "${LIMIT}")
fi

REMOTE_COMMAND="${REMOTE_PYTHON} -B -m tools.tuning.search_weights \
  --exports exports \
  --default-weights configs/evaluation_weights/default.json \
  --split train \
  --fixed-depth ${FIXED_DEPTH} \
  --time-budget-ms ${TIME_BUDGET_MS} \
  --trials ${TRIALS} \
  --storage sqlite:///artifacts/weight_tuning/optuna.db \
  --study-name opponent-pressure-v1 \
  --output artifacts/weight_tuning/best_weights.json \
  ${LIMIT_ARGS[*]} \
  > artifacts/weight_tuning/search.log 2>&1"

ssh "${REMOTE}" "cd '${REMOTE_DIR}' && test -x .venv/bin/python || python3 -m venv .venv"

ssh "${REMOTE}" "cd '${REMOTE_DIR}' && '${REMOTE_PYTHON}' -m pip show optuna setuptools >/dev/null 2>&1 || '${REMOTE_PYTHON}' -m pip install optuna setuptools"

ssh "${REMOTE}" "cd '${REMOTE_DIR}' && '${REMOTE_PYTHON}' setup.py build_ext --inplace --force"

ssh "${REMOTE}" "cd '${REMOTE_DIR}' && mkdir -p artifacts/weight_tuning"

if ssh "${REMOTE}" "command -v tmux >/dev/null 2>&1"; then
  ssh "${REMOTE}" "cd '${REMOTE_DIR}' && tmux new-session -d -s '${SESSION}' \"${REMOTE_COMMAND}\""
  echo "Started remote tmux session ${SESSION} on ${REMOTE}"
  echo "Watch: ssh ${REMOTE} 'tmux attach -t ${SESSION}'"
else
  ssh "${REMOTE}" "cd '${REMOTE_DIR}' && setsid bash -lc \"${REMOTE_COMMAND}\" </dev/null >/dev/null 2>&1 & echo \$! > '${REMOTE_DIR}/artifacts/weight_tuning/search.pid'"
  echo "Started remote background process on ${REMOTE}"
  echo "Watch: ssh ${REMOTE} 'tail -f ${REMOTE_DIR}/artifacts/weight_tuning/search.log'"
fi

echo "Fetch: rsync -az ${REMOTE}:${REMOTE_DIR}/artifacts/weight_tuning/ artifacts/weight_tuning/"
