#!/usr/bin/env bash
# One-command setup and launch for pathos:
#   builds the C++ engine, installs Python deps, makes sure a graph exists,
#   builds the React app and serves everything on http://localhost:8000
#
#   ./run.sh                 # uses data/processed/jaipur-v1.graph (or demo-v1 offline)
#   PATHOS_GRAPH=data/processed/demo-v1.graph ./run.sh
set -euo pipefail
cd "$(dirname "$0")"

PY="${PYTHON:-python3}"

echo "==> building C++ engine"
engine/build.sh >/dev/null
engine/build/pathos_tests

echo "==> Python environment"
if [ ! -x .venv/bin/python ]; then "$PY" -m venv .venv; fi
.venv/bin/pip install -q -r backend/requirements.txt

if ! ls data/processed/*.graph >/dev/null 2>&1; then
  echo "==> no graph found; downloading Jaipur from OpenStreetMap (falls back to the offline demo graph)"
  .venv/bin/python scripts/prepare_graph.py || .venv/bin/python scripts/make_demo_graph.py
fi

if command -v npm >/dev/null 2>&1; then
  echo "==> building frontend"
  (cd frontend && npm install --silent && npm run build --silent) \
    || echo "!! frontend build failed; serving the API only (http://localhost:8000/docs)"
else
  echo "!! npm not found: API only (open http://localhost:8000/docs)"
fi

echo "==> pathos running at http://localhost:8000"
exec .venv/bin/uvicorn app.main:app --app-dir backend --host 127.0.0.1 --port "${PORT:-8000}"
