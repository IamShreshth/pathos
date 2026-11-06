#include "pathos/sim.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <map>
#include <queue>
#include <random>

namespace pathos {

namespace {
using Clock = std::chrono::steady_clock;
double ms_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}
double quantile(std::vector<double> v, double q) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    const double pos = q * static_cast<double>(v.size() - 1);
    const std::size_t lo = static_cast<std::size_t>(pos), hi = std::min(lo + 1, v.size() - 1);
    return v[lo] + (v[hi] - v[lo]) * (pos - static_cast<double>(lo));
}
}  // namespace

const char* to_string(Strategy s) {
    switch (s) {
        case Strategy::Static: return "static";
        case Strategy::Greedy: return "greedy";
        case Strategy::Full: return "full";
        case Strategy::Selective: return "selective";
    }
    return "?";
}
bool parse_strategy(const std::string& s, Strategy& out) {
    if (s == "static") out = Strategy::Static;
    else if (s == "greedy") out = Strategy::Greedy;
    else if (s == "full") out = Strategy::Full;
    else if (s == "selective") out = Strategy::Selective;
    else return false;
    return true;
}

// ------------------------------------------------------------ disruptions
Disruptions build_disruptions(const Graph& g, const TrafficModel& tm, const std::vector<DisruptionEvent>& ev,
                              double until_time) {
    Disruptions d(tm.edge_count());
    for (const DisruptionEvent& e : ev) {
        if (e.time > until_time) continue;
        for (const auto& [u, v] : e.roads) {
            const int k = find_edge(g, u, v);
            if (k < 0) continue;
            const int eid = tm.first_edge(u) + k;
            if (e.closure) d.add_closure(eid, e.time, e.until);
            else d.add_slowdown(eid, e.time, e.until, e.factor);
        }
    }
    return d;
}

// -------------------------------------------------------- world generator
namespace {

// Nodes from which `depot` can be reached and which can be reached from `depot`.
std::vector<char> mutual_reach(const Graph& g, NodeId depot, const std::vector<std::vector<NodeId>>& radj) {
    const std::size_t n = g.node_count();
    auto reach = [&](bool forward) {
        std::vector<char> seen(n, 0);
        std::vector<NodeId> st{depot};
        seen[static_cast<std::size_t>(depot)] = 1;
        while (!st.empty()) {
            const NodeId u = st.back();
            st.pop_back();
            if (forward) {
                for (const Edge& e : g.neighbors(u))
                    if (!seen[static_cast<std::size_t>(e.to)]) { seen[static_cast<std::size_t>(e.to)] = 1; st.push_back(e.to); }
            } else {
                for (NodeId v : radj[static_cast<std::size_t>(u)])
                    if (!seen[static_cast<std::size_t>(v)]) { seen[static_cast<std::size_t>(v)] = 1; st.push_back(v); }
            }
        }
        return seen;
    };
    const auto f = reach(true), b = reach(false);
    std::vector<char> both(n, 0);
    for (std::size_t i = 0; i < n; ++i) both[i] = f[i] && b[i];
    return both;
}

}  // namespace

void finish_world(const Graph& g, const TrafficModel& tm, World& w, const WorldConfig& cfg, bool windows, bool events) {
    if (w.vehicles.empty()) return;
    std::mt19937_64 rng(static_cast<std::uint64_t>(cfg.seed) * 6271 + 5);
    auto U = [&](double a, double b) { return std::uniform_real_distribution<double>(a, b)(rng); };
    const std::size_t n = g.node_count();
    const NodeId depot = w.vehicles[0].depot;
    const double shift_end = w.vehicles[0].shift_end;

    // Travel times from the depot at the start of the shift (mean traffic) place realistic windows.
    Net net{&g, &tm, nullptr};
    TdQuery q;
    q.source = depot;
    q.depart = w.t_start + 1800;
    TdSearch tree;
    td_search(net, q, tree);

    if (windows) {
        const int nreq = static_cast<int>(w.requests.size());
        for (int i = 0; i < nreq; ++i) {
            Request& r = w.requests[static_cast<std::size_t>(i)];
            const double travel = std::max(0.0, tree.arr[static_cast<std::size_t>(r.node)] - q.depart);
            const double width = cfg.window_min * 60.0 * U(0.7, 1.3);
            const bool dynamic = i >= static_cast<int>(std::round(nreq * (1.0 - cfg.dynamic_fraction)));
            if (dynamic) {
                r.release_s = w.t_start + U(600, 6000);
                r.tw_start = r.release_s + travel * 0.7 + U(300, 1800);
            } else {
                r.release_s = w.t_start;
                r.tw_start = w.t_start + travel * 0.7 + U(0, 5400);
            }
            r.tw_end = std::min(r.tw_start + width, shift_end - 1500);
            r.tw_start = std::min(r.tw_start, r.tw_end - 600);
        }
    }
    if (!events) return;

    // Disruptions on the roads that most shortest paths from the depot share (the busy corridors).
    std::map<std::pair<NodeId, NodeId>, int> usage;
    for (const Request& r : w.requests) {
        for (NodeId v = r.node; v != depot && tree.parent[static_cast<std::size_t>(v)] != kNoNode; v = tree.parent[static_cast<std::size_t>(v)])
            ++usage[{tree.parent[static_cast<std::size_t>(v)], v}];
    }
    std::vector<std::pair<int, std::pair<NodeId, NodeId>>> ranked;
    for (const auto& kv : usage) ranked.push_back({kv.second, kv.first});
    std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first != b.first ? a.first > b.first : a.second < b.second; });
    std::vector<char> taken(n, 0);
    auto pick = [&]() -> std::pair<NodeId, NodeId> {
        std::vector<std::pair<NodeId, NodeId>> eligible;
        for (const auto& r : ranked) {
            const auto [u, v] = r.second;
            if (taken[static_cast<std::size_t>(u)] || taken[static_cast<std::size_t>(v)] || u == depot || v == depot) continue;
            eligible.push_back(r.second);
            if (eligible.size() >= 15) break;
        }
        if (eligible.empty()) return {kNoNode, kNoNode};
        const auto chosen = eligible[std::uniform_int_distribution<std::size_t>(0, eligible.size() - 1)(rng)];
        taken[static_cast<std::size_t>(chosen.first)] = taken[static_cast<std::size_t>(chosen.second)] = 1;
        return chosen;
    };
    auto label_for = [&](const char* what, NodeId u) {
        char buf[96];
        std::snprintf(buf, sizeof buf, "%s near %.4f, %.4f", what, g.node(u).lat, g.node(u).lon);
        return std::string(buf);
    };
    for (int i = 0; i < cfg.closures; ++i) {
        const auto [u, v] = pick();
        if (u == kNoNode) break;
        DisruptionEvent d;
        d.time = w.t_start + U(900, 5400);
        d.until = d.time + U(3600, 7200);
        d.closure = true;
        d.roads.push_back({u, v});
        if (find_edge(g, v, u) >= 0) d.roads.push_back({v, u});
        d.label = label_for("road closure", u);
        w.events.push_back(d);
    }
    for (int i = 0; i < cfg.incidents; ++i) {
        const auto [u, v] = pick();
        if (u == kNoNode) break;
        DisruptionEvent d;
        d.time = w.t_start + U(900, 5400);
        d.until = d.time + U(2400, 4800);
        d.closure = false;
        d.factor = U(2.5, 3.5);
        d.roads.push_back({u, v});
        for (const Edge& e1 : g.neighbors(v)) {  // the queue spills onto the next roads
            if (d.roads.size() >= 8) break;
            d.roads.push_back({v, e1.to});
        }
        d.label = label_for("accident", u);
        w.events.push_back(d);
    }
    std::sort(w.events.begin(), w.events.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
}

World generate_world(const Graph& g, const TrafficModel& tm, const WorldConfig& cfg) {
    World w;
    w.t_start = cfg.t_start;
    std::mt19937_64 rng(static_cast<std::uint64_t>(cfg.seed) * 7919 + 13);
    const std::size_t n = g.node_count();
    if (n == 0) return w;

    std::vector<std::vector<NodeId>> radj(n);
    for (std::size_t u = 0; u < n; ++u)
        for (const Edge& e : g.neighbors(static_cast<NodeId>(u))) radj[static_cast<std::size_t>(e.to)].push_back(static_cast<NodeId>(u));
    double s = 90, wl = 180, nn = -90, e = -180;
    for (std::size_t i = 0; i < n; ++i) {
        const Node& nd = g.node(static_cast<NodeId>(i));
        s = std::min(s, nd.lat); nn = std::max(nn, nd.lat); wl = std::min(wl, nd.lon); e = std::max(e, nd.lon);
    }
    const double clat = (s + nn) / 2, clon = (wl + e) / 2;
    std::vector<std::pair<double, NodeId>> by_center;
    for (std::size_t i = 0; i < n; ++i)
        by_center.push_back({haversine_m(g.node(static_cast<NodeId>(i)).lat, g.node(static_cast<NodeId>(i)).lon, clat, clon), static_cast<NodeId>(i)});
    std::sort(by_center.begin(), by_center.end());
    // The depot is a central node that can reach (and be reached from) most of the graph.
    NodeId depot = by_center[0].second;
    std::vector<char> good;
    std::size_t best_size = 0;
    for (std::size_t c = 0; c < std::min<std::size_t>(12, n); ++c) {
        auto both = mutual_reach(g, by_center[c].second, radj);
        std::size_t cnt = 0;
        for (char x : both) cnt += x;
        if (cnt > best_size) { best_size = cnt; good = both; depot = by_center[c].second; }
        if (cnt * 10 >= n * 9) break;
    }
    std::vector<NodeId> pool;
    for (std::size_t i = 0; i < n; ++i) {
        if (!good[i] || static_cast<NodeId>(i) == depot) continue;
        const Node& a = g.node(static_cast<NodeId>(i));
        if (cfg.radius_m > 0 && haversine_m(a.lat, a.lon, g.node(depot).lat, g.node(depot).lon) > cfg.radius_m) continue;
        pool.push_back(static_cast<NodeId>(i));
    }
    std::shuffle(pool.begin(), pool.end(), rng);
    const int nreq = std::min<int>(cfg.requests, static_cast<int>(pool.size()));

    double total_demand = 0;
    for (int i = 0; i < nreq; ++i) {
        Request r;
        r.id = i;
        r.node = pool[static_cast<std::size_t>(i)];
        r.demand = static_cast<double>(std::uniform_int_distribution<int>(1, 3)(rng));
        r.service_s = std::uniform_real_distribution<double>(90, 240)(rng);
        total_demand += r.demand;
        w.requests.push_back(r);
    }
    const double cap = std::max(3.0, std::ceil(total_demand / std::max(1, cfg.vehicles) * 1.6));
    const double shift_end = cfg.t_start + cfg.shift_hours * 3600.0;
    for (int v = 0; v < cfg.vehicles; ++v) w.vehicles.push_back({v, depot, cap, cfg.t_start, shift_end});
    finish_world(g, tm, w, cfg, true, true);
    return w;
}

// -------------------------------------------------------------- execution
namespace {

struct RunStop {
    int ri = -1;
    Path leg;
};

struct VRun {
    NodeId node = kNoNode;
    double time = 0;
    double load = 0;
    bool used = false;
    bool stuck = false;
    std::deque<RunStop> stops;
    Path ret;
    std::size_t leg_pos = 0;
    double dist = 0, driving = 0, energy = 0;
    Trajectory traj;
};

struct Exec {
    const Graph* g;
    const TrafficModel* tm;
    const Disruptions* dis;  // the real world
    const World* w;
    const FleetParams* fp;
    EvalMode mode;           // realised scenario
};

void advance(const Exec& ex, const Vehicle& veh, VRun& v, double T, std::vector<RequestOutcome>& out) {
    const Net wn{ex.g, ex.tm, ex.dis};
    for (;;) {
        const Path* leg = !v.stops.empty() ? &v.stops.front().leg : (v.ret.edges() > 0 ? &v.ret : nullptr);
        if (!leg || v.stuck) return;
        while (v.leg_pos < leg->edges()) {
            if (v.time >= T) return;
            const int k = leg->ks[v.leg_pos];
            const double t2 = wn.arrive(v.node, k, v.time, ex.mode);
            if (std::isinf(t2)) { v.stuck = true; return; }
            const double len = ex.g->neighbors(v.node)[static_cast<std::size_t>(k)].length_m;
            v.dist += len;
            v.driving += t2 - v.time;
            v.energy += len / 1000.0 * ex.fp->kwh_per_km * (1.0 + 0.6 * v.load / std::max(1.0, veh.capacity));
            v.time = t2;
            v.node = leg->nodes[v.leg_pos + 1];
            ++v.leg_pos;
            v.traj.nodes.push_back(v.node);
            v.traj.times.push_back(v.time);
        }
        if (!v.stops.empty()) {
            const int ri = v.stops.front().ri;
            const Request& R = ex.w->requests[static_cast<std::size_t>(ri)];
            RequestOutcome& o = out[static_cast<std::size_t>(ri)];
            const double arrival = v.time;
            o.served = true;
            o.vehicle = veh.id;
            o.arrival = arrival;
            o.lateness = std::max(0.0, arrival - R.tw_end);
            o.on_time = o.lateness <= 1e-6;
            o.response = arrival - std::max(R.release_s, ex.w->t_start);
            v.time = std::max(arrival, R.tw_start) + R.service_s;
            v.load += R.demand;
            v.used = true;
            v.stops.pop_front();
            v.leg_pos = 0;
            v.traj.nodes.push_back(v.node);
            v.traj.times.push_back(v.time);
        } else {
            v.ret = Path{};
            v.leg_pos = 0;
        }
    }
}

struct Sim {
    const Graph& g;
    const TrafficModel& tm;
    const World& w;
    const SimConfig& cfg;
    Disruptions world_dis, known;
    Exec ex;
    std::vector<VRun> vs;
    std::vector<int> assigned;     // request -> vehicle index or -1
    std::vector<char> released;
    std::vector<RequestOutcome> out;
    SimResult res;

    Sim(const Graph& g_, const TrafficModel& tm_, const World& w_, const SimConfig& c)
        : g(g_), tm(tm_), w(w_), cfg(c), world_dis(build_disruptions(g_, tm_, w_.events)), known(tm_.edge_count()) {
        ex = Exec{&g, &tm, &world_dis, &w, &cfg.fp, EvalMode{}};
        ex.mode.scenario = cfg.scenario;
        vs.resize(w.vehicles.size());
        for (std::size_t i = 0; i < vs.size(); ++i) {
            vs[i].node = w.vehicles[i].depot;
            vs[i].time = w.t_start;
            vs[i].traj.vid = w.vehicles[i].id;
            vs[i].traj.nodes.push_back(vs[i].node);
            vs[i].traj.times.push_back(vs[i].time);
        }
        assigned.assign(w.requests.size(), -1);
        released.assign(w.requests.size(), 0);
        out.resize(w.requests.size());
        for (std::size_t i = 0; i < out.size(); ++i) out[i].id = w.requests[i].id;
    }

    Net plan_net() const { return Net{&g, &tm, &known}; }
    EvalMode plan_mode() const { EvalMode m; m.kappa = cfg.fp.kappa; return m; }
    bool served(int ri) const { return out[static_cast<std::size_t>(ri)].served; }

    int iterations_for(std::size_t n) const {
        return static_cast<int>(std::min<std::size_t>(6000, std::max<std::size_t>(200, n * static_cast<std::size_t>(cfg.iters_per_request))));
    }

    void advance_all(double T) {
        for (std::size_t i = 0; i < vs.size(); ++i) advance(ex, w.vehicles[i], vs[i], T, out);
        for (std::size_t i = 0; i < vs.size(); ++i)  // served requests leave the assignment table
            for (std::size_t r = 0; r < assigned.size(); ++r)
                if (out[r].served && assigned[r] == static_cast<int>(i)) assigned[r] = -2;
    }

    bool pending(int ri, double now) const {
        return released[static_cast<std::size_t>(ri)] && !served(ri) && assigned[static_cast<std::size_t>(ri)] == -1 &&
               w.requests[static_cast<std::size_t>(ri)].tw_end > now;
    }

    std::vector<int> stops_of(int vi) const {
        std::vector<int> s;
        for (const RunStop& rs : vs[static_cast<std::size_t>(vi)].stops) s.push_back(rs.ri);
        return s;
    }

    // Routes `seq` for vehicle vi from its current node with legs computed on the planner's network.
    // Requests with unreachable legs are returned as dropped.
    std::vector<int> install(int vi, const std::vector<int>& seq, double now) {
        VRun& v = vs[static_cast<std::size_t>(vi)];
        const Net pn = plan_net();
        const EvalMode pm = plan_mode();
        NodeId cur = v.node;
        double t = std::max(v.time, now);
        std::deque<RunStop> ns;
        std::vector<int> dropped;
        for (int ri : seq) {
            const Request& R = w.requests[static_cast<std::size_t>(ri)];
            TdResult leg = td_route(pn, cur, R.node, t, pm, true);
            if (!leg.found) { dropped.push_back(ri); assigned[static_cast<std::size_t>(ri)] = -1; continue; }
            t = std::max(leg.arrival, R.tw_start) + R.service_s;
            cur = R.node;
            ns.push_back({ri, std::move(leg.path)});
            assigned[static_cast<std::size_t>(ri)] = vi;
        }
        v.ret = Path{};
        if (cur != w.vehicles[static_cast<std::size_t>(vi)].depot) {
            TdResult back = td_route(pn, cur, w.vehicles[static_cast<std::size_t>(vi)].depot, t, pm, true);
            if (back.found) v.ret = std::move(back.path);
        }
        v.stops = std::move(ns);
        v.leg_pos = 0;
        return dropped;
    }

    Problem make_sub(const std::vector<int>& vids, const std::vector<int>& ris, double now) const {
        Problem P;
        P.now = now;
        std::map<int, int> local;
        for (int ri : ris) {
            const Request& R = w.requests[static_cast<std::size_t>(ri)];
            local[ri] = static_cast<int>(P.req.size());
            P.req.push_back({ri, R.node, R.demand, R.tw_start, R.tw_end, R.service_s});
        }
        P.prev_veh.assign(P.req.size(), -1);
        P.prev_rank.assign(P.req.size(), -1);
        for (std::size_t k = 0; k < vids.size(); ++k) {
            const int vi = vids[k];
            const VRun& v = vs[static_cast<std::size_t>(vi)];
            const Vehicle& V = w.vehicles[static_cast<std::size_t>(vi)];
            SubVehicle s;
            s.vid = vi;
            s.pos = v.node;
            s.avail = std::max(v.time, now);
            s.depot = V.depot;
            s.cap_total = V.capacity;
            s.cap_left = V.capacity - v.load;
            s.shift_end = V.shift_end;
            s.pays_fixed = !(v.used || v.dist > 0);
            P.veh.push_back(s);
            std::vector<int> warm;
            int rank = 0;
            for (const RunStop& rs : v.stops) {
                auto it = local.find(rs.ri);
                if (it == local.end()) continue;
                warm.push_back(it->second);
                P.prev_veh[static_cast<std::size_t>(it->second)] = static_cast<int>(k);
                P.prev_rank[static_cast<std::size_t>(it->second)] = rank++;
            }
            P.warm.push_back(std::move(warm));
        }
        return P;
    }

    // Solve a sub-problem and install the routes of the vehicles whose plan changed (or `force`).
    ReplanLog replan(const std::vector<int>& vids, const std::vector<int>& ris, double now, PlanMode mode, double beta,
                     bool force, const std::string& kind, const std::string& label) {
        const auto t0 = Clock::now();
        ReplanLog log;
        log.time = now;
        log.kind = kind;
        log.label = label;
        log.vehicles = static_cast<int>(vids.size());
        log.requests = static_cast<int>(ris.size());
        log.replanned = true;
        Problem P = make_sub(vids, ris, now);
        FleetParams fp = cfg.fp;
        fp.beta = beta;
        PlanStats st;
        Solution sol = plan_problem(plan_net(), P, fp, mode, iterations_for(ris.size()), &st);
        log.matrix_ms = st.matrix_ms;
        log.solve_ms = st.solve_ms;
        log.disruption = schedule_disruption(P, sol.routes);
        const auto tl = Clock::now();
        for (std::size_t k = 0; k < vids.size(); ++k) {
            const int vi = vids[k];
            std::vector<int> seq;
            for (int r : sol.routes[k]) seq.push_back(P.req[static_cast<std::size_t>(r)].rid);
            const bool changed = seq != stops_of(vi);
            if (changed || force) {
                for (int ri : stops_of(vi)) assigned[static_cast<std::size_t>(ri)] = -1;
                install(vi, seq, now);
            }
        }
        log.legs_ms = ms_since(tl);
        log.latency_ms = ms_since(t0);
        if (kind == "initial") {
            res.initial_routes.clear();
            for (std::size_t k = 0; k < vids.size(); ++k) {
                std::vector<int> seq;
                for (int r : sol.routes[k]) seq.push_back(P.req[static_cast<std::size_t>(r)].rid);
                res.initial_routes.push_back(seq);
            }
            res.initial_cost = sol.cost;
            res.initial_unassigned = sol.unassigned();
            res.initial_stats = st;
        }
        return log;
    }

    std::vector<int> all_vehicles() const {
        std::vector<int> v(vs.size());
        for (std::size_t i = 0; i < v.size(); ++i) v[i] = static_cast<int>(i);
        return v;
    }

    // Requests that are assigned to one of `vids` or waiting for a vehicle.
    std::vector<int> scope_requests(const std::vector<int>& vids, double now) const {
        std::vector<int> ris;
        for (int vi : vids)
            for (int ri : stops_of(vi)) ris.push_back(ri);
        for (std::size_t ri = 0; ri < w.requests.size(); ++ri)
            if (pending(static_cast<int>(ri), now)) ris.push_back(static_cast<int>(ri));
        std::sort(ris.begin(), ris.end());
        ris.erase(std::unique(ris.begin(), ris.end()), ris.end());
        return ris;
    }

    bool affected(int vi, const Disruptions& fresh) const {
        const VRun& v = vs[static_cast<std::size_t>(vi)];
        auto check = [&](const Path& p, std::size_t from) {
            for (std::size_t i = from; i < p.edges(); ++i) {
                const int eid = tm.first_edge(p.nodes[i]) + p.ks[i];
                if (!fresh.flagged(eid)) continue;
                for (const Span& s : *fresh.spans(eid))
                    if (s.to > v.time) return true;
            }
            return false;
        };
        bool first = true;
        for (const RunStop& rs : v.stops) {
            if (check(rs.leg, first ? v.leg_pos : 0)) return true;
            first = false;
        }
        if (v.stops.empty() && check(v.ret, v.leg_pos)) return true;
        if (!v.stops.empty() && check(v.ret, 0)) return true;
        return false;
    }

    void on_request(int ri, double now) {
        released[static_cast<std::size_t>(ri)] = 1;
        const std::string label = "request " + std::to_string(w.requests[static_cast<std::size_t>(ri)].id);
        ReplanLog log;
        switch (cfg.strategy) {
            case Strategy::Static:
            case Strategy::Greedy:
                log = replan(all_vehicles(), scope_requests(all_vehicles(), now), now, PlanMode::InsertOnly, 0.0, false, "request", label);
                break;
            case Strategy::Full:
                log = replan(all_vehicles(), scope_requests(all_vehicles(), now), now, PlanMode::Scratch, 0.0, true, "request", label);
                break;
            case Strategy::Selective: {
                const Request& R = w.requests[static_cast<std::size_t>(ri)];
                std::vector<std::pair<double, int>> near;
                for (std::size_t vi = 0; vi < vs.size(); ++vi) {
                    const Node& a = g.node(vs[vi].node);
                    near.push_back({haversine_m(a.lat, a.lon, g.node(R.node).lat, g.node(R.node).lon), static_cast<int>(vi)});
                }
                std::sort(near.begin(), near.end());
                std::vector<int> vids;
                for (std::size_t i = 0; i < near.size() && static_cast<int>(i) < cfg.selective_k; ++i) vids.push_back(near[i].second);
                log = replan(vids, scope_requests(vids, now), now, PlanMode::Warm, cfg.beta, false, "request", label);
                if (assigned[static_cast<std::size_t>(ri)] < 0 && vids.size() < vs.size()) {
                    // Not placeable among the nearby vehicles: widen to the whole fleet.
                    ReplanLog wide = replan(all_vehicles(), scope_requests(all_vehicles(), now), now, PlanMode::Warm, cfg.beta,
                                            false, "request", label);
                    wide.latency_ms += log.latency_ms;
                    wide.matrix_ms += log.matrix_ms;
                    wide.solve_ms += log.solve_ms;
                    wide.legs_ms += log.legs_ms;
                    wide.disruption += log.disruption;
                    wide.escalated = true;
                    log = wide;
                }
                break;
            }
        }
        res.log.push_back(log);
    }

    void on_disruption(const DisruptionEvent& d, double now) {
        const std::string label = d.label;
        ReplanLog log;
        log.time = now;
        log.kind = "disruption";
        log.label = label;
        if (cfg.strategy == Strategy::Static) {  // unaware: it finds out when its vehicles get stuck
            res.log.push_back(log);
            return;
        }
        const auto t0 = Clock::now();
        std::vector<DisruptionEvent> one{d};
        const Disruptions fresh = build_disruptions(g, tm, one);
        known.merge(fresh);
        if (cfg.strategy == Strategy::Full) {
            log = replan(all_vehicles(), scope_requests(all_vehicles(), now), now, PlanMode::Scratch, 0.0, true, "disruption", label);
        } else {
            std::vector<int> hit;
            for (std::size_t vi = 0; vi < vs.size(); ++vi)
                if (affected(static_cast<int>(vi), fresh)) hit.push_back(static_cast<int>(vi));
            if (hit.empty()) {
                log.latency_ms = ms_since(t0);  // detection only: nothing to recompute
            } else if (cfg.strategy == Strategy::Greedy) {
                const auto tl = Clock::now();
                log.replanned = true;
                log.vehicles = static_cast<int>(hit.size());
                for (int vi : hit) install(vi, stops_of(vi), now);  // same order, new paths
                log.legs_ms = ms_since(tl);
                log.latency_ms = ms_since(t0);
            } else {
                log = replan(hit, scope_requests(hit, now), now, PlanMode::Warm, cfg.beta, true, "disruption", label);
            }
        }
        res.log.push_back(log);
    }

    void finish() {
        SimMetrics& m = res.m;
        m.requests = static_cast<int>(w.requests.size());
        std::vector<double> resp;
        for (const RequestOutcome& o : out) {
            if (!o.served) { ++m.unserved; continue; }
            if (o.on_time) ++m.on_time; else ++m.late;
            m.lateness_s += o.lateness;
            resp.push_back(o.response);
        }
        m.success_rate = m.requests ? static_cast<double>(m.on_time) / m.requests : 0;
        if (!resp.empty()) {
            double s = 0;
            for (double r : resp) s += r;
            m.avg_response_s = s / static_cast<double>(resp.size());
            m.p95_response_s = quantile(resp, 0.95);
        }
        for (const VRun& v : vs) {
            m.total_distance_m += v.dist;
            m.driving_s += v.driving;
            m.energy_kwh += v.energy;
            if (v.dist > 0) ++m.vehicles_used;
            m.makespan_s = std::max(m.makespan_s, v.time - w.t_start);
        }
        const FleetParams& fp = cfg.fp;
        m.cost = fp.w_time * m.driving_s + fp.w_dist * m.total_distance_m + fp.w_energy * m.energy_kwh +
                 fp.w_late * m.lateness_s + fp.unassigned_penalty * m.unserved + fp.vehicle_fixed * m.vehicles_used;
        for (const ReplanLog& l : res.log) {
            if (l.kind == "initial") { m.initial_ms = l.latency_ms; continue; }
            if (!l.replanned) continue;
            ++m.replans;
            m.total_latency_ms += l.latency_ms;
            m.max_latency_ms = std::max(m.max_latency_ms, l.latency_ms);
            m.schedule_disruption += l.disruption;
            if (l.escalated) ++m.escalations;
        }
        m.mean_latency_ms = m.replans ? m.total_latency_ms / m.replans : 0.0;
        res.outcomes = out;
        for (VRun& v : vs) res.trajectories.push_back(std::move(v.traj));
    }

    void initial_plan() {
        std::vector<int> ris;
        for (std::size_t i = 0; i < w.requests.size(); ++i)
            if (w.requests[i].release_s <= w.t_start) { released[i] = 1; ris.push_back(static_cast<int>(i)); }
        const int iters = cfg.initial_iterations > 0 ? cfg.initial_iterations : iterations_for(ris.size());
        const auto t0 = Clock::now();
        Problem P = make_sub(all_vehicles(), ris, w.t_start);
        FleetParams fp = cfg.fp;
        fp.beta = 0;
        PlanStats st;
        Solution sol = plan_problem(plan_net(), P, fp, PlanMode::Scratch, iters, &st);
        ReplanLog log;
        log.time = w.t_start;
        log.kind = "initial";
        log.label = "initial plan";
        log.vehicles = static_cast<int>(vs.size());
        log.requests = static_cast<int>(ris.size());
        log.matrix_ms = st.matrix_ms;
        log.solve_ms = st.solve_ms;
        log.replanned = true;
        for (std::size_t k = 0; k < vs.size(); ++k) {
            std::vector<int> seq;
            for (int r : sol.routes[k]) seq.push_back(P.req[static_cast<std::size_t>(r)].rid);
            res.initial_routes.push_back(seq);
            install(static_cast<int>(k), seq, w.t_start);
        }
        res.initial_cost = sol.cost;
        res.initial_unassigned = sol.unassigned();
        res.initial_stats = st;
        log.latency_ms = ms_since(t0);
        res.log.push_back(log);
    }
};

}  // namespace

SimResult simulate(const Graph& g, const TrafficModel& tm, const World& w, const SimConfig& cfg) {
    Sim sim(g, tm, w, cfg);
    sim.res.strategy = cfg.strategy;
    sim.initial_plan();

    struct Ev { double time; int kind; int idx; };
    std::vector<Ev> evs;
    for (std::size_t i = 0; i < w.requests.size(); ++i)
        if (w.requests[i].release_s > w.t_start) evs.push_back({w.requests[i].release_s, 0, static_cast<int>(i)});
    for (std::size_t i = 0; i < w.events.size(); ++i) evs.push_back({w.events[i].time, 1, static_cast<int>(i)});
    std::stable_sort(evs.begin(), evs.end(), [](const Ev& a, const Ev& b) { return a.time < b.time; });

    for (const Ev& e : evs) {
        sim.advance_all(e.time);
        if (e.kind == 0) sim.on_request(e.idx, e.time);
        else sim.on_disruption(w.events[static_cast<std::size_t>(e.idx)], e.time);
    }
    sim.advance_all(1e9);
    sim.finish();
    return std::move(sim.res);
}

McPlanStats evaluate_plan_mc(const Graph& g, const TrafficModel& tm, const World& w, const SimConfig& cfg,
                             int scenarios, bool include_disruptions) {
    // Plan once with the requests known at the start (the planner does not see future disruptions).
    World known = w;
    known.requests.clear();
    std::vector<int> keep;
    for (std::size_t i = 0; i < w.requests.size(); ++i)
        if (w.requests[i].release_s <= w.t_start) { known.requests.push_back(w.requests[i]); keep.push_back(static_cast<int>(i)); }
    if (!include_disruptions) known.events.clear();
    for (std::size_t i = 0; i < known.requests.size(); ++i) known.requests[i].id = static_cast<int>(i);

    SimConfig c = cfg;
    Sim base(g, tm, known, c);
    base.initial_plan();
    McPlanStats st;
    st.scenarios = scenarios;
    const std::size_t nr = known.requests.size();
    std::vector<double> ok(nr, 0.0), rates, spans;
    for (int s = 0; s < scenarios; ++s) {
        std::vector<VRun> runs = base.vs;
        std::vector<RequestOutcome> out(nr);
        Exec ex = base.ex;
        ex.mode.scenario = 1000 + s;
        double dist = 0, span = 0;
        for (std::size_t i = 0; i < runs.size(); ++i) {
            runs[i].traj = Trajectory{};
            advance(ex, known.vehicles[i], runs[i], 1e9, out);
            dist += runs[i].dist;
            span = std::max(span, runs[i].time - known.t_start);
        }
        int hits = 0;
        for (std::size_t r = 0; r < nr; ++r)
            if (out[r].served && out[r].on_time) { ok[r] += 1; ++hits; }
        rates.push_back(nr ? static_cast<double>(hits) / static_cast<double>(nr) : 0.0);
        spans.push_back(span);
        st.mean_distance_m += dist / scenarios;
    }
    double sum = 0, sp = 0;
    for (double r : rates) sum += r;
    for (double x : spans) sp += x;
    st.on_time_rate = scenarios ? sum / scenarios : 0;
    st.on_time_rate_p5 = quantile(rates, 0.05);
    st.mean_makespan_s = scenarios ? sp / scenarios : 0;
    st.p95_makespan_s = quantile(spans, 0.95);
    st.per_request_on_time.assign(w.requests.size(), 0.0);
    for (std::size_t r = 0; r < nr; ++r) st.per_request_on_time[static_cast<std::size_t>(keep[r])] = ok[r] / std::max(1, scenarios);
    return st;
}

}  // namespace pathos
