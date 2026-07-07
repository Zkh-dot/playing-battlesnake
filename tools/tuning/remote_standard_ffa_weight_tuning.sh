#!/usr/bin/env bash
set -euo pipefail

REMOTE="${REMOTE:-scv@192.168.1.6}"
REMOTE_DIR="${REMOTE_DIR:-/tmp/playing-battlesnake-standard-ffa-tuning}"
SESSION="${SESSION:-battlesnake-standard-ffa-tuning}"
TRIALS="${TRIALS:-220}"
GAMES="${GAMES:-4}"
MAX_TURNS="${MAX_TURNS:-80}"
MIN_FOOD="${MIN_FOOD:-3}"
SEED="${SEED:-20260707}"
TRAIN_SEEDS="${TRAIN_SEEDS:-7000,9000,11000,13000,15000}"
SEARCH_MODE="${SEARCH_MODE:-mutate}"
MUTATION_SCALE="${MUTATION_SCALE:-0.16}"
LATENCY_BUDGET_MS="${LATENCY_BUDGET_MS:-80}"
REMOTE_PYTHON="${REMOTE_DIR}/.venv/bin/python"
ARTIFACT_DIR="artifacts/standard_ffa_weight_tuning"
SSH_OPTS=(-o ServerAliveInterval=30 -o ServerAliveCountMax=20)

require_positive_int() {
  local name="$1"
  local value="$2"
  if [[ ! "${value}" =~ ^[1-9][0-9]*$ ]]; then
    echo "${name} must be a positive integer" >&2
    exit 2
  fi
}

require_number() {
  local name="$1"
  local value="$2"
  if [[ ! "${value}" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
    echo "${name} must be a non-negative number" >&2
    exit 2
  fi
}

case "${SEARCH_MODE}" in
  mutate|random) ;;
  *)
    echo "SEARCH_MODE must be mutate or random" >&2
    exit 2
    ;;
esac

require_positive_int TRIALS "${TRIALS}"
require_positive_int GAMES "${GAMES}"
require_positive_int MAX_TURNS "${MAX_TURNS}"
require_positive_int MIN_FOOD "${MIN_FOOD}"
require_positive_int SEED "${SEED}"
require_number MUTATION_SCALE "${MUTATION_SCALE}"
require_number LATENCY_BUDGET_MS "${LATENCY_BUDGET_MS}"

mkdir -p "${ARTIFACT_DIR}"

rsync -az --delete \
  --exclude '.git/' \
  --exclude '__pycache__/' \
  --include 'battlesnake/battlesnake_native*.so' \
  --exclude '*.so' \
  --exclude 'build/' \
  --exclude '.venv/' \
  --exclude 'artifacts/standard_ffa_weight_tuning/' \
  ./ "${REMOTE}:${REMOTE_DIR}/"

REMOTE_COMMAND="${REMOTE_PYTHON} -B -m tools.tuning.search_standard_ffa_weights \
  --search-mode ${SEARCH_MODE} \
  --mutation-scale ${MUTATION_SCALE} \
  --trials ${TRIALS} \
  --games ${GAMES} \
  --max-turns ${MAX_TURNS} \
  --min-food ${MIN_FOOD} \
  --seed ${SEED} \
  --train-seeds ${TRAIN_SEEDS} \
  --latency-budget-ms ${LATENCY_BUDGET_MS} \
  --output ${ARTIFACT_DIR}/standard-ffa-v1-tuned.json \
  --trials-output ${ARTIFACT_DIR}/trials.jsonl \
  > ${ARTIFACT_DIR}/search.log 2>&1"

ssh "${SSH_OPTS[@]}" "${REMOTE}" "cd '${REMOTE_DIR}' && test -x .venv/bin/python || python3 -m venv .venv"
ssh "${SSH_OPTS[@]}" "${REMOTE}" "cd '${REMOTE_DIR}' && '${REMOTE_PYTHON}' -m pip show setuptools >/dev/null 2>&1 || '${REMOTE_PYTHON}' -m pip install setuptools"
ssh "${SSH_OPTS[@]}" "${REMOTE}" "cd '${REMOTE_DIR}' && '${REMOTE_PYTHON}' setup.py -q build_ext --inplace --force"
ssh "${SSH_OPTS[@]}" "${REMOTE}" "cd '${REMOTE_DIR}' && mkdir -p '${ARTIFACT_DIR}'"

if ssh "${SSH_OPTS[@]}" "${REMOTE}" "command -v tmux >/dev/null 2>&1"; then
  ssh "${SSH_OPTS[@]}" "${REMOTE}" "cd '${REMOTE_DIR}' && tmux new-session -d -s '${SESSION}' \"${REMOTE_COMMAND}\""
  echo "Started remote tmux session ${SESSION} on ${REMOTE}"
  echo "Watch: ssh ${REMOTE} 'tmux attach -t ${SESSION}'"
else
  ssh "${SSH_OPTS[@]}" "${REMOTE}" "cd '${REMOTE_DIR}' && setsid bash -lc \"${REMOTE_COMMAND}\" </dev/null >/dev/null 2>&1 & echo \$! > '${REMOTE_DIR}/${ARTIFACT_DIR}/search.pid'"
  echo "Started remote background process on ${REMOTE}"
  echo "Watch: ssh ${REMOTE} 'tail -f ${REMOTE_DIR}/${ARTIFACT_DIR}/search.log'"
fi

echo "Fetch: rsync -az ${REMOTE}:${REMOTE_DIR}/${ARTIFACT_DIR}/ ${ARTIFACT_DIR}/"
