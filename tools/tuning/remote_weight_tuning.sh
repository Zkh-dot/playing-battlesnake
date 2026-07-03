#!/usr/bin/env bash
set -euo pipefail

REMOTE="${REMOTE:-scv@192.168.1.6}"
REMOTE_DIR="${REMOTE_DIR:-/tmp/playing-battlesnake-weight-tuning}"
SESSION="${SESSION:-battlesnake-weight-tuning}"
TRIALS="${TRIALS:-200}"
FIXED_DEPTH="${FIXED_DEPTH:-3}"
TIME_BUDGET_MS="${TIME_BUDGET_MS:-5000}"
LIMIT="${LIMIT:-}"

mkdir -p artifacts/weight_tuning

rsync -az --delete \
  --exclude '.git/' \
  --exclude '__pycache__/' \
  --exclude '*.so' \
  --exclude 'build/' \
  --exclude 'artifacts/weight_tuning/' \
  ./ "${REMOTE}:${REMOTE_DIR}/"

LIMIT_ARGS=()
if [[ -n "${LIMIT}" ]]; then
  LIMIT_ARGS=(--limit "${LIMIT}")
fi

ssh "${REMOTE}" "cd '${REMOTE_DIR}' && python3 -m pip show optuna >/dev/null 2>&1 || python3 -m pip install --user optuna"

ssh "${REMOTE}" "cd '${REMOTE_DIR}' && python3 setup.py build_ext --inplace --force"

ssh "${REMOTE}" "cd '${REMOTE_DIR}' && tmux new-session -d -s '${SESSION}' \
  'python3 -B -m tools.tuning.search_weights \
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
    > artifacts/weight_tuning/search.log 2>&1'"

echo "Started remote tmux session ${SESSION} on ${REMOTE}"
echo "Watch: ssh ${REMOTE} 'tmux attach -t ${SESSION}'"
echo "Fetch: rsync -az ${REMOTE}:${REMOTE_DIR}/artifacts/weight_tuning/ artifacts/weight_tuning/"
