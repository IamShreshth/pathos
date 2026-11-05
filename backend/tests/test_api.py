"""Integration tests: FastAPI -> C++ engine subprocess -> JSON responses.

Requires the engine to be built (engine/build.sh). Uses a tiny hand-made graph
so expected answers are known exactly.
"""

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
from app.services.graph_store import haversine_m, largest_scc  # noqa: E402

ENGINE = ROOT / "engine" / "build" / "pathos_engine"
pytestmark = pytest.mark.skipif(not ENGINE.exists(), reason="engine not built; run engine/build.sh")

# Layout (~110 m spacing), plus an island 5 <-> 6 that is unreachable from 0..4.
#
#   3 ---------- 4
#   |            |
#   0 --- 1 ---> 2      1 -> 2 is one-way
NODES = [
    (10.000, 10.000, 0),
    (10.000, 10.001, 1),
    (10.000, 10.002, 2),
    (10.001, 10.000, 3),
    (10.001, 10.002, 4),
    (10.010, 10.010, 5),
    (10.010, 10.011, 6),
]


def _edge(u: int, v: int) -> EdgeRecord:
    d = haversine_m(NODES[u][0], NODES[u][1], NODES[v][0], NODES[v][1]) * 1.05
    return EdgeRecord(u, v, d, d / 10.0)


EDGES = [
    _edge(0, 1), _edge(1, 0),
    _edge(1, 2),  # one-way
    _edge(2, 4), _edge(4, 2),
    _edge(0, 3), _edge(3, 0),
    _edge(3, 4), _edge(4, 3),
    _edge(5, 6), _edge(6, 5),
]


def pt(i: int) -> dict:
    return {"lat": NODES[i][0], "lon": NODES[i][1]}


def _make_client(tmp_path_factory, snap_to_main_component: bool):
    graph = tmp_path_factory.mktemp("graph") / "test-v1.graph"
    write_graph(graph, "test-v1", NODES, EDGES, {"source": "unit test"})
    settings = replace(
        Settings.from_env(),
        graph_path=graph,
        engine_path=ENGINE,
        max_tour_stops=5,
        snap_to_main_component=snap_to_main_component,
        data_dir=graph.parent,
        frontend_dist=tmp_path_factory.mktemp("nodist"),
    )
    return TestClient(create_app(settings))


@pytest.fixture(scope="module")
def client(tmp_path_factory):
    """Snaps to any node, so the engine's unreachable handling is exercised."""
    with _make_client(tmp_path_factory, snap_to_main_component=False) as c:
        yield c


@pytest.fixture(scope="module")
def scc_client(tmp_path_factory):
    """Default production behaviour: snap only into the largest strongly connected component."""
    with _make_client(tmp_path_factory, snap_to_main_component=True) as c:
        yield c


def test_health_and_metadata(client):
    assert client.get("/api/v1/health").json() == {"status": "ok", "engine": True, "graph_version": "test-v1"}
    meta = client.get("/api/v1/graph/metadata").json()
    assert meta["nodes"] == 7 and meta["edges"] == len(EDGES)
    assert meta["graph_version"] == "test-v1"


def test_compare_algorithms_agree(client):
    r = client.post("/api/v1/routes/compare", json={"source": pt(0), "destination": pt(2)})
    assert r.status_code == 200, r.text
    body = r.json()
    res = {x["algorithm"]: x for x in body["results"]}
    assert set(res) == {"bfs", "dijkstra", "astar"}
    for x in res.values():
        assert x["status"] == "success"
        assert x["route_node_ids"] == [0, 1, 2]
        assert x["geometry"]["coordinates"][0] == [NODES[0][1], NODES[0][0]]  # GeoJSON [lon, lat]
        assert len(x["trace"]) == x["expanded_nodes"]
    assert res["dijkstra"]["cost"] == pytest.approx(res["astar"]["cost"])
    assert res["astar"]["expanded_nodes"] <= res["dijkstra"]["expanded_nodes"]


def test_one_way_is_respected(client):
    r = client.post("/api/v1/routes", json={"source": pt(2), "destination": pt(0), "algorithm": "dijkstra"})
    route = r.json()["results"][0]
    assert route["route_node_ids"] == [2, 4, 3, 0]  # cannot go back along 1 -> 2


def test_unreachable(client):
    r = client.post("/api/v1/routes/compare", json={"source": pt(0), "destination": pt(5), "record_trace": False})
    assert r.status_code == 200
    for x in r.json()["results"]:
        assert x["status"] == "unreachable"
        assert x["geometry"] is None and x["cost"] is None and x["trace"] is None


def test_source_equals_destination(client):
    r = client.post("/api/v1/routes", json={"source": pt(3), "destination": pt(3)})
    x = r.json()["results"][0]
    assert x["status"] == "success" and x["cost"] == 0 and x["route_node_ids"] == [3]


def test_snapping_and_validation(client):
    near = {"lat": NODES[4][0] + 0.0001, "lon": NODES[4][1]}
    r = client.post("/api/v1/routes", json={"source": pt(0), "destination": near})
    assert r.json()["destination"]["node_id"] == 4
    assert 5 < r.json()["destination"]["snap_distance_m"] < 20

    far = {"lat": 11.0, "lon": 11.0}
    r = client.post("/api/v1/routes", json={"source": pt(0), "destination": far})
    assert r.status_code == 422
    detail = r.json()["detail"]
    assert detail["code"] == "outside_coverage"
    assert detail["label"] == "destination" and detail["point"] == far
    assert detail["distance_to_area_m"] > 100_000
    assert "outside the loaded map area" in detail["message"]
    bad = {"lat": 123, "lon": 0}
    assert client.post("/api/v1/routes", json={"source": bad, "destination": pt(0)}).status_code == 422
    assert client.post("/api/v1/routes", json={"source": pt(0), "destination": pt(1), "algorithm": "x"}).status_code == 422


def test_tour(client):
    r = client.post("/api/v1/tours", json={"stops": [pt(0), pt(2), pt(4), pt(3)]})
    assert r.status_code == 200, r.text
    t = r.json()
    assert t["status"] == "success"
    assert t["order"][0] == 0 and sorted(t["order"]) == [0, 1, 2, 3]
    assert len(t["legs"]) == 4 and all(l["reachable"] for l in t["legs"])
    assert t["cost"] <= t["nn_cost"] + 1e-6
    assert t["optimal_cost"] == pytest.approx(t["cost"])
    assert t["distance_m"] == pytest.approx(sum(l["distance_m"] for l in t["legs"]))


def test_tour_with_unreachable_stop(client):
    t = client.post("/api/v1/tours", json={"stops": [pt(0), pt(2), pt(5)]}).json()
    assert t["status"] == "unreachable"
    assert t["cost"] is None
    assert len(t["unreachable_legs"]) == 2


def test_tour_limits(client):
    assert client.post("/api/v1/tours", json={"stops": [pt(0)]}).status_code == 422
    assert client.post("/api/v1/tours", json={"stops": [pt(0)] * 6}).status_code == 422


def test_engine_restarts_after_crash(client):
    engine = client.app.state.graphs.active.engine
    engine._proc.kill()
    engine._proc.wait()
    r = client.post("/api/v1/routes", json={"source": pt(0), "destination": pt(4)})
    assert r.status_code == 200
    assert engine.alive


def test_largest_scc():
    # 0 <-> 1 -> 2 <-> 3, plus 2 -> 4 (sink): SCCs {0,1}, {2,3}, {4}; ties pick the first found.
    adj = [[1], [0, 2], [3, 4], [2], []]
    mask = largest_scc(adj)
    assert sum(mask) == 2 and mask[4] is False
    # Directed cycle 0 -> 1 -> 2 -> 0 with a tail 3 -> 0: SCC is the cycle.
    assert largest_scc([[1], [2], [0], [0]]) == [True, True, True, False]
    assert largest_scc([]) == []


def test_snapping_into_main_component(scc_client):
    meta = scc_client.get("/api/v1/graph/metadata").json()
    assert meta["main_component_nodes"] == 5 and meta["snappable_nodes"] == 5
    # The island (nodes 5, 6) is not strongly connected to the rest: clicks there are rejected
    # instead of producing an unreachable route.
    r = scc_client.post("/api/v1/routes", json={"source": pt(0), "destination": pt(5)})
    assert r.status_code == 422
    t = scc_client.post("/api/v1/tours", json={"stops": [pt(0), pt(2), pt(4)]}).json()
    assert t["status"] == "success"


# ---------------------------------------------------------------- map areas
SHIFT = 1.0  # a second area one degree north-east of the test graph


def _shifted_nodes():
    return [(la + SHIFT, lo + SHIFT, i) for la, lo, i in NODES]


def test_list_and_select_graphs(scc_client):
    data_dir = scc_client.app.state.settings.data_dir
    write_graph(data_dir / "other-v1.graph", "other-v1", _shifted_nodes(), EDGES, {"source": "unit test"})
    graphs = {g["name"]: g for g in scc_client.get("/api/v1/graphs").json()}
    assert graphs["test-v1"]["active"] and not graphs["other-v1"]["active"]

    meta = scc_client.post("/api/v1/graphs/select", json={"name": "other-v1"}).json()
    assert meta["graph_version"] == "other-v1"
    shifted = {"lat": NODES[0][0] + SHIFT, "lon": NODES[0][1] + SHIFT}
    r = scc_client.post("/api/v1/routes", json={"source": shifted, "destination": shifted})
    assert r.status_code == 200 and r.json()["graph_version"] == "other-v1"

    assert scc_client.post("/api/v1/graphs/select", json={"name": "../etc/passwd"}).status_code == 422
    assert scc_client.post("/api/v1/graphs/select", json={"name": "missing"}).status_code == 404
    scc_client.post("/api/v1/graphs/select", json={"name": "test-v1"})


def test_load_area_downloads_and_switches(scc_client, monkeypatch):
    import app.services.graph_manager as gm

    calls = []

    def fake_build(*, point, dist, name, out_dir, **_):
        calls.append((point, dist, name))
        write_graph(out_dir / f"{name}.graph", name, _shifted_nodes(), EDGES, {"source": "fake download"})

    monkeypatch.setattr(gm.prepare_graph, "build_graph", fake_build)
    monkeypatch.setattr(gm.prepare_graph, "geocode", lambda place: (NODES[0][0] + SHIFT, NODES[0][1] + SHIFT))

    centre = {"lat": NODES[0][0] + SHIFT, "lon": NODES[0][1] + SHIFT}
    r = scc_client.post("/api/v1/graphs/area", json={"center": centre, "radius_m": 1000})
    assert r.status_code == 200, r.text
    assert r.json()["graph_version"].startswith("area_")
    assert len(calls) == 1

    # The same area again is reused, not downloaded twice.
    assert scc_client.post("/api/v1/graphs/area", json={"center": centre, "radius_m": 1000}).status_code == 200
    assert len(calls) == 1

    r = scc_client.post("/api/v1/graphs/area", json={"place": "Test Town", "radius_m": 1000})
    assert r.json()["graph_version"] == "test-town-1000m"

    # The point that used to be "outside coverage" now routes.
    r = scc_client.post("/api/v1/routes", json={"source": centre, "destination": centre})
    assert r.status_code == 200
    scc_client.post("/api/v1/graphs/select", json={"name": "test-v1"})


def test_load_area_validation(scc_client):
    c = pt(0)
    assert scc_client.post("/api/v1/graphs/area", json={"radius_m": 1000}).status_code == 422
    assert scc_client.post("/api/v1/graphs/area", json={"center": c, "place": "x y"}).status_code == 422
    assert scc_client.post("/api/v1/graphs/area", json={"center": c, "radius_m": 10**6}).status_code == 422
    assert scc_client.post("/api/v1/graphs/area", json={"center": c, "radius_m": 10}).status_code == 422
