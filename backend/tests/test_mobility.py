"""Integration tests for the Pathos 2.0 endpoints (FastAPI -> C++ engine) on a small grid city."""

from __future__ import annotations

import sys
from dataclasses import replace
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from graph_format import EdgeRecord, write_graph  # noqa: E402

from app.config import Settings  # noqa: E402
from app.main import create_app  # noqa: E402
from app.services.graph_store import haversine_m  # noqa: E402

ENGINE = ROOT / "engine" / "build" / "pathos_engine"
pytestmark = pytest.mark.skipif(not ENGINE.exists(), reason="engine not built; run engine/build.sh")

W = H = 12
STEP = 0.0009  # ~100 m


def _node(x: int, y: int) -> int:
    return y * W + x


NODES = [(26.9 + y * STEP, 75.8 + x * STEP, y * W + x) for y in range(H) for x in range(W)]


def _edges() -> list[EdgeRecord]:
    out = []
    for y in range(H):
        for x in range(W):
            for nx, ny in ((x + 1, y), (x, y + 1)):
                if nx >= W or ny >= H:
                    continue
                u, v = _node(x, y), _node(nx, ny)
                d = haversine_m(NODES[u][0], NODES[u][1], NODES[v][0], NODES[v][1]) * 1.05
                speed = 7.0 + 6.0 * (((x * 7 + y * 13 + nx * 3) % 10) / 10.0)
                out += [EdgeRecord(u, v, d, d / speed), EdgeRecord(v, u, d, d / speed)]
    return out


@pytest.fixture(scope="module")
def client(tmp_path_factory):
    graph = tmp_path_factory.mktemp("grid") / "grid-v1.graph"
    write_graph(graph, "grid-v1", NODES, _edges(), {"source": "unit test grid"})
    settings = replace(
        Settings.from_env(),
        graph_path=graph,
        engine_path=ENGINE,
        data_dir=graph.parent,
        frontend_dist=tmp_path_factory.mktemp("nodist"),
        max_snap_distance_m=300,
    )
    with TestClient(create_app(settings)) as c:
        yield c


def pt(x: int, y: int) -> dict:
    n = NODES[_node(x, y)]
    return {"lat": n[0], "lon": n[1]}


def test_traffic_profile_and_map(client):
    r = client.get("/api/v1/traffic", params={"hour": 8})
    assert r.status_code == 200, r.text
    body = r.json()
    assert len(body["profile"]) == 48
    assert body["profile"][16] > body["profile"][6]  # 08:00 is busier than 03:00
    assert body["synthetic"] is True
    assert sum(len(lv["coordinates"]) for lv in body["levels"]) > 0
    night = client.get("/api/v1/traffic", params={"hour": 3}).json()
    assert sum(len(lv["coordinates"]) for lv in night["levels"]) == 0  # free-flow at night


def test_mobility_routes(client):
    r = client.post("/api/v1/mobility/routes", json={"source": pt(0, 0), "destination": pt(11, 10), "scenarios": 100})
    assert r.status_code == 200, r.text
    b = r.json()
    cands = b["candidates"]
    assert len(cands) >= 3
    for key in ("freeflow", "expected", "robust", "reliable"):
        assert 0 <= b[key] < len(cands)
    for c in cands:
        assert 0 <= c["p_on_time"] <= 1
        assert c["mc_p50_s"] <= c["mc_p95_s"] <= c["mc_worst_s"] + 1e-6
        assert c["expected_s"] >= c["free_s"] - 1e-6
        assert len(c["geometry"]["coordinates"]) >= 2
    ex = cands[b["expected"]]
    assert all(ex["expected_s"] <= c["expected_s"] + 1e-6 for c in cands)
    if b["reliable_feasible"]:
        assert cands[b["reliable"]]["p_on_time"] >= 1 - b["alpha"] - 1e-9


def test_mobility_routes_rejects_far_points(client):
    r = client.post("/api/v1/mobility/routes", json={"source": pt(0, 0), "destination": {"lat": 40.0, "lon": 10.0}})
    assert r.status_code == 422
    assert r.json()["detail"]["code"] == "outside_coverage"


def test_replan(client):
    r = client.post("/api/v1/mobility/replan", json={
        "source": pt(0, 0), "destination": pt(11, 11), "type": "closure", "duration_min": 90, "edges_ahead": 6,
    })
    assert r.status_code == 200, r.text
    b = r.json()
    assert len(b["disrupted"]) >= 1
    # Waiting at the closure is the worst option; recomputation is optimal; repair lies in between.
    assert b["full"]["arrival_s"] <= b["selective"]["arrival_s"] + 1e-6 <= b["static"]["arrival_s"] + 1e-6
    assert b["static"]["arrival_s"] - b["original"]["arrival_s"] > 1000  # ~90 minute closure
    assert b["selective"]["affected"] is True


def test_fleet_generate(client):
    r = client.post("/api/v1/fleet/simulate", json={
        "generate": {"seed": 2, "vehicles": 3, "requests": 14, "closures": 1, "incidents": 1, "window_min": 40},
        "strategies": ["static", "selective"], "iters_per_request": 10, "mc_scenarios": 20,
    })
    assert r.status_code == 200, r.text
    b = r.json()
    assert len(b["requests"]) == 14 and len(b["vehicles"]) == 3
    assert [x["strategy"] for x in b["results"]] == ["static", "selective"]
    for res in b["results"]:
        m = res["metrics"]
        assert m["on_time"] + m["late"] + m["unserved"] == m["requests"] == 14
        for t in res["trajectories"]:
            times = [p[2] for p in t["points"]]
            assert times == sorted(times)
    assert 0 <= b["mc"]["on_time_rate"] <= 1
    assert len(b["mc"]["per_request_on_time"]) == 14


def test_fleet_custom_and_validation(client):
    stops = [pt(x, y) for x, y in [(2, 2), (9, 3), (4, 9), (10, 10), (1, 7), (7, 7)]]
    r = client.post("/api/v1/fleet/simulate", json={
        "custom": {"depot": pt(5, 5), "stops": stops, "vehicles": 2, "dynamic_fraction": 0.3},
        "strategies": ["full"], "iters_per_request": 10,
    })
    assert r.status_code == 200, r.text
    assert len(r.json()["requests"]) == 6
    assert client.post("/api/v1/fleet/simulate", json={}).status_code == 422
    both = {"generate": {}, "custom": {"depot": pt(5, 5), "stops": stops}}
    assert client.post("/api/v1/fleet/simulate", json=both).status_code == 422
    assert client.post("/api/v1/fleet/simulate", json={"generate": {"requests": 9999}}).status_code == 422
