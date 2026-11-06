#include "pathos/tdroute.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <queue>
#include <unordered_set>

namespace pathos {

namespace {
using Clock = std::chrono::steady_clock;
double ms_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}
struct QItem {
    double key;
    NodeId node;
    bool operator>(const QItem& o) const { return key > o.key; }
};
}  // namespace

int find_edge(const Graph& g, NodeId u, NodeId v) {
    int best = -1;
    const auto& adj = g.neighbors(u);
    for (std::size_t k = 0; k < adj.size(); ++k)
        if (adj[k].to == v && (best < 0 || adj[k].time_s < adj[static_cast<std::size_t>(best)].time_s))
            best = static_cast<int>(k);
    return best;
}

Path path_from_nodes(const Graph& g, const std::vector<NodeId>& nodes) {
    Path p;
    p.nodes = nodes;
    for (std::size_t i = 0; i + 1 < nodes.size(); ++i) p.ks.push_back(find_edge(g, nodes[i], nodes[i + 1]));
    return p;
}

void td_search(const Net& net, const TdQuery& q, TdSearch& out) {
    const Graph& g = *net.g;
    const std::size_t n = g.node_count();
    out.arr.assign(n, kInf);
    out.parent.assign(n, kNoNode);
    out.pk.assign(n, -1);
    out.expanded = 0;
    out.trace.clear();
    out.trace_truncated = false;

    // A* needs a lower bound; scenario times can be shorter than the mean, so they use plain Dijkstra.
    const bool use_astar = q.astar && q.target != kNoNode && q.mode.scenario < 0;
    auto h = [&](NodeId u) { return use_astar ? g.heuristic(u, q.target, Weight::Time) : 0.0; };

    std::vector<char> settled(n, 0), is_target;
    std::size_t remaining = 0;
    if (q.targets) {
        is_target.assign(n, 0);
        for (NodeId t : *q.targets)
            if (!is_target[static_cast<std::size_t>(t)]) { is_target[static_cast<std::size_t>(t)] = 1; ++remaining; }
    }

    std::priority_queue<QItem, std::vector<QItem>, std::greater<QItem>> pq;
    out.arr[static_cast<std::size_t>(q.source)] = q.depart;
    pq.push({q.depart + h(q.source), q.source});
    while (!pq.empty()) {
        const NodeId u = pq.top().node;
        pq.pop();
        if (settled[static_cast<std::size_t>(u)]) continue;
        settled[static_cast<std::size_t>(u)] = 1;
        ++out.expanded;
        if (q.trace) {
            if (out.trace.size() < q.max_trace) out.trace.push_back({u, out.parent[static_cast<std::size_t>(u)]});
            else out.trace_truncated = true;
        }
        if (u == q.target) break;
        if (q.targets && is_target[static_cast<std::size_t>(u)] && --remaining == 0) break;
        const double t = out.arr[static_cast<std::size_t>(u)];
        const auto& adj = g.neighbors(u);
        for (std::size_t k = 0; k < adj.size(); ++k) {
            const NodeId v = adj[k].to;
            if (settled[static_cast<std::size_t>(v)]) continue;
            const double a = net.arrive(u, static_cast<int>(k), t, q.mode);
            if (a < out.arr[static_cast<std::size_t>(v)]) {
                out.arr[static_cast<std::size_t>(v)] = a;
                out.parent[static_cast<std::size_t>(v)] = u;
                out.pk[static_cast<std::size_t>(v)] = static_cast<int>(k);
                pq.push({a + h(v), v});
            }
        }
    }
}

Path extract_path(const TdSearch& s, NodeId source, NodeId target) {
    Path p;
    if (s.arr[static_cast<std::size_t>(target)] == kInf) return p;
    for (NodeId v = target; v != kNoNode; v = s.parent[static_cast<std::size_t>(v)]) {
        p.nodes.push_back(v);
        if (v == source) break;
        p.ks.push_back(s.pk[static_cast<std::size_t>(v)]);
    }
    if (p.nodes.empty() || p.nodes.back() != source) return {};
    std::reverse(p.nodes.begin(), p.nodes.end());
    std::reverse(p.ks.begin(), p.ks.end());
    return p;
}

double path_length_m(const Graph& g, const Path& p) {
    double d = 0;
    for (std::size_t i = 0; i < p.ks.size(); ++i)
        d += g.neighbors(p.nodes[i])[static_cast<std::size_t>(p.ks[i])].length_m;
    return d;
}

double walk_path(const Net& net, const Path& p, std::size_t from_idx, double t, const EvalMode& mode) {
    for (std::size_t i = from_idx; i < p.ks.size(); ++i) {
        t = net.arrive(p.nodes[i], p.ks[i], t, mode);
        if (std::isinf(t)) return kInf;
    }
    return t;
}

TdResult td_route(const Net& net, NodeId source, NodeId target, double depart, const EvalMode& mode, bool astar,
                  bool trace) {
    const auto t0 = Clock::now();
    TdQuery q;
    q.source = source;
    q.target = target;
    q.depart = depart;
    q.mode = mode;
    q.astar = astar;
    q.trace = trace;
    TdSearch s;
    td_search(net, q, s);
    TdResult r;
    r.depart = depart;
    r.expanded = s.expanded;
    r.trace = std::move(s.trace);
    r.trace_truncated = s.trace_truncated;
    if (s.arr[static_cast<std::size_t>(target)] < kInf) {
        r.found = true;
        r.arrival = s.arr[static_cast<std::size_t>(target)];
        r.travel_s = r.arrival - depart;
        r.path = extract_path(s, source, target);
        r.distance_m = path_length_m(*net.g, r.path);
    }
    r.duration_ms = ms_since(t0);
    return r;
}

// ------------------------------------------------------------ uncertainty
double normal_quantile(double p) {
    // Peter Acklam's rational approximation, |relative error| < 1.2e-9.
    static const double a[] = {-3.969683028665376e+01, 2.209460984245205e+02, -2.759285104469687e+02,
                               1.383577518672690e+02, -3.066479806614716e+01, 2.506628277459239e+00};
    static const double b[] = {-5.447609879822406e+01, 1.615858368580409e+02, -1.556989798598866e+02,
                               6.680131188771972e+01, -1.328068155288572e+01};
    static const double c[] = {-7.784894002430293e-03, -3.223964580411365e-01, -2.400758277161838e+00,
                               -2.549732539343734e+00, 4.374664141464968e+00, 2.938163982698783e+00};
    static const double d[] = {7.784695709041462e-03, 3.224671290700398e-01, 2.445134137142996e+00,
                               3.754408661907416e+00};
    p = std::min(std::max(p, 1e-12), 1 - 1e-12);
    const double pl = 0.02425;
    if (p < pl) {
        const double q = std::sqrt(-2 * std::log(p));
        return (((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) /
               ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1);
    }
    if (p > 1 - pl) return -normal_quantile(1 - p);
    const double q = p - 0.5, r = q * q;
    return (((((a[0] * r + a[1]) * r + a[2]) * r + a[3]) * r + a[4]) * r + a[5]) * q /
           (((((b[0] * r + b[1]) * r + b[2]) * r + b[3]) * r + b[4]) * r + 1);
}

namespace {
double quantile_sorted(const std::vector<double>& v, double q) {
    if (v.empty()) return 0;
    const double pos = q * static_cast<double>(v.size() - 1);
    const std::size_t lo = static_cast<std::size_t>(pos);
    const std::size_t hi = std::min(lo + 1, v.size() - 1);
    return v[lo] + (v[hi] - v[lo]) * (pos - static_cast<double>(lo));
}

std::uint64_t path_hash(const Path& p) {
    std::uint64_t h = 1469598103934665603ULL;
    for (std::size_t i = 0; i < p.ks.size(); ++i) h = mix64(h ^ (static_cast<std::uint64_t>(p.nodes[i]) << 8) ^ static_cast<std::uint64_t>(p.ks[i]));
    return h;
}
}  // namespace

ReliabilityResult reliability_routes(const Net& net, const ReliabilityQuery& q) {
    const auto t0 = Clock::now();
    ReliabilityResult res;
    std::unordered_set<std::uint64_t> seen;
    auto add = [&](const std::string& label, double kappa, const Path& p) -> int {
        if (p.nodes.size() < 1) return -1;
        const std::uint64_t h = path_hash(p);
        if (!seen.insert(h).second) {
            for (std::size_t i = 0; i < res.candidates.size(); ++i)
                if (path_hash(res.candidates[i].path) == h) return static_cast<int>(i);
            return -1;
        }
        RouteStats s;
        s.label = label;
        s.kappa = kappa;
        s.path = p;
        res.candidates.push_back(std::move(s));
        return static_cast<int>(res.candidates.size()) - 1;
    };

    EvalMode free_mode;
    free_mode.freeflow = true;
    // Free-flow route: what a static router that ignores traffic would choose.
    TdResult ff = td_route(net, q.source, q.target, q.depart, free_mode, true);
    TdResult ex = td_route(net, q.source, q.target, q.depart, EvalMode{}, true);
    if (!ff.found || !ex.found) {
        res.duration_ms = ms_since(t0);
        return res;
    }
    res.freeflow = add("free-flow", 0.0, ff.path);
    res.expected = add("expected", 0.0, ex.path);
    const double z = normal_quantile(1.0 - q.alpha);
    EvalMode rm;
    rm.kappa = std::max(z, 0.0);
    TdResult rb = td_route(net, q.source, q.target, q.depart, rm, true);
    res.robust = add("robust", rm.kappa, rb.path);
    for (double k : {0.5, 1.0, 2.0, 3.0}) {
        EvalMode m;
        m.kappa = k;
        add("kappa " + std::to_string(k).substr(0, 3), k, td_route(net, q.source, q.target, q.depart, m, true).path);
    }
    for (int s = 0; s < q.candidate_scenarios; ++s) {
        EvalMode m;
        m.scenario = s;  // scenario-optimal routes: the best path in one sampled world
        add("scenario " + std::to_string(s), -1.0, td_route(net, q.source, q.target, q.depart, m, false).path);
    }

    res.deadline_s = q.deadline_s > 0 ? q.deadline_s : 1.15 * ex.travel_s;
    const int base = 100000;  // evaluation scenarios are disjoint from the generation scenarios
    std::vector<double> dur(static_cast<std::size_t>(q.scenarios));
    for (RouteStats& s : res.candidates) {
        s.distance_m = path_length_m(*net.g, s.path);
        s.free_s = walk_path(net, s.path, 0, q.depart, free_mode) - q.depart;
        s.expected_s = walk_path(net, s.path, 0, q.depart, EvalMode{}) - q.depart;
        int ok = 0;
        double sum = 0, sq = 0;
        for (int i = 0; i < q.scenarios; ++i) {
            EvalMode m;
            m.scenario = base + i;
            dur[static_cast<std::size_t>(i)] = walk_path(net, s.path, 0, q.depart, m) - q.depart;
            sum += dur[static_cast<std::size_t>(i)];
            sq += dur[static_cast<std::size_t>(i)] * dur[static_cast<std::size_t>(i)];
            if (dur[static_cast<std::size_t>(i)] <= res.deadline_s) ++ok;
        }
        const double nn = std::max(1, q.scenarios);
        s.mc_mean = sum / nn;
        s.mc_std = std::sqrt(std::max(0.0, sq / nn - s.mc_mean * s.mc_mean));
        std::sort(dur.begin(), dur.end());
        s.mc_p50 = quantile_sorted(dur, 0.5);
        s.mc_p95 = quantile_sorted(dur, 0.95);
        s.mc_worst = dur.empty() ? 0 : dur.back();
        s.p_on_time = ok / nn;
    }

    // Reliability-constrained choice: the fastest (in expectation) candidate
    // that satisfies P(T <= D) >= 1 - alpha; otherwise the most reliable one.
    int best = -1;
    for (std::size_t i = 0; i < res.candidates.size(); ++i) {
        const RouteStats& s = res.candidates[i];
        if (s.p_on_time >= 1.0 - q.alpha && (best < 0 || s.mc_mean < res.candidates[static_cast<std::size_t>(best)].mc_mean))
            best = static_cast<int>(i);
    }
    res.reliable_feasible = best >= 0;
    if (best < 0) {
        for (std::size_t i = 0; i < res.candidates.size(); ++i) {
            const RouteStats& s = res.candidates[i];
            if (best < 0 || s.p_on_time > res.candidates[static_cast<std::size_t>(best)].p_on_time + 1e-12 ||
                (std::fabs(s.p_on_time - res.candidates[static_cast<std::size_t>(best)].p_on_time) <= 1e-12 &&
                 s.mc_mean < res.candidates[static_cast<std::size_t>(best)].mc_mean))
                best = static_cast<int>(i);
        }
    }
    res.reliable = best;
    res.duration_ms = ms_since(t0);
    return res;
}

// ------------------------------------------------------------------ repair
RepairResult local_repair(const Net& net, const Disruptions& fresh, const Path& old, double t_now, int lookback) {
    const auto t0 = Clock::now();
    RepairResult out;
    const Graph& g = *net.g;
    const std::size_t m = old.ks.size();
    // Arrival time at every node of the remaining route under the current network.
    std::vector<double> at(m + 1);
    at[0] = t_now;
    for (std::size_t i = 0; i < m; ++i) at[i + 1] = net.arrive(old.nodes[i], old.ks[i], at[i], EvalMode{});

    long first = -1, last = -1;
    for (std::size_t i = 0; i < m; ++i) {
        const int eid = net.tm->first_edge(old.nodes[i]) + old.ks[i];
        if (!fresh.flagged(eid)) continue;
        bool touches = false;
        for (const Span& s : *fresh.spans(eid))
            if (s.to > at[i]) touches = true;  // the disruption is still (or about to be) in force
        if (!touches) continue;
        if (first < 0) first = static_cast<long>(i);
        last = static_cast<long>(i);
    }
    if (first < 0) {
        out.path = old;
        out.arrival = at[m];
        out.duration_ms = ms_since(t0);
        return out;
    }
    out.affected = true;

    // Search from the first affected node; rejoin the old route after the last affected edge.
    const std::size_t a = static_cast<std::size_t>(std::max(0L, first - std::max(0, lookback))), b = static_cast<std::size_t>(last);
    // Free-flow time of the old route's tail from each node: a lower bound on its remaining duration
    // (congestion and slowdowns only add time), used to skip exact evaluation of hopeless rejoins.
    std::vector<double> ffs(m + 1, 0.0);
    for (std::size_t i = m; i-- > 0;) ffs[i] = ffs[i + 1] + g.neighbors(old.nodes[i])[static_cast<std::size_t>(old.ks[i])].time_s;
    const std::size_t n = g.node_count();
    std::vector<int> pos(n, -1);
    for (std::size_t j = b + 1; j <= m; ++j) pos[static_cast<std::size_t>(old.nodes[j])] = static_cast<int>(j);

    std::vector<double> arr(n, kInf);
    std::vector<NodeId> parent(n, kNoNode);
    std::vector<int> pk(n, -1);
    std::vector<char> settled(n, 0);
    std::priority_queue<QItem, std::vector<QItem>, std::greater<QItem>> pq;
    const NodeId src = old.nodes[a];
    const NodeId dest = old.nodes.back();
    // Goal-directed: key = arrival + straight-line bound to the destination (admissible and consistent).
    auto h = [&](NodeId u) { return g.heuristic(u, dest, Weight::Time); };
    arr[static_cast<std::size_t>(src)] = at[a];
    pq.push({at[a] + h(src), src});
    double best_total = kInf;
    long best_j = -1;
    while (!pq.empty()) {
        const QItem it = pq.top();
        pq.pop();
        if (it.key >= best_total) break;  // nothing later can beat the best splice found
        const NodeId u = it.node;
        if (settled[static_cast<std::size_t>(u)]) continue;
        settled[static_cast<std::size_t>(u)] = 1;
        ++out.expanded;
        const int j = pos[static_cast<std::size_t>(u)];
        if (j >= 0 && arr[static_cast<std::size_t>(u)] + ffs[static_cast<std::size_t>(j)] < best_total) {
            const double total = walk_path(net, old, static_cast<std::size_t>(j), arr[static_cast<std::size_t>(u)], EvalMode{});
            if (total < best_total) {
                best_total = total;
                best_j = j;
            }
        }
        const auto& adj = g.neighbors(u);
        for (std::size_t k = 0; k < adj.size(); ++k) {
            const NodeId v = adj[k].to;
            if (settled[static_cast<std::size_t>(v)]) continue;
            const double t2 = net.arrive(u, static_cast<int>(k), arr[static_cast<std::size_t>(u)], EvalMode{});
            if (t2 < arr[static_cast<std::size_t>(v)]) {
                arr[static_cast<std::size_t>(v)] = t2;
                parent[static_cast<std::size_t>(v)] = u;
                pk[static_cast<std::size_t>(v)] = static_cast<int>(k);
                pq.push({t2 + h(v), v});
            }
        }
    }

    if (best_j < 0) {  // no rejoin point is reachable: escalate to a full search from the vehicle's node
        out.escalated = true;
        TdResult full = td_route(net, old.nodes.front(), old.nodes.back(), t_now, EvalMode{}, true);
        out.expanded += full.expanded;
        out.repaired = full.found;
        out.path = full.path;
        out.arrival = full.arrival;
        out.duration_ms = ms_since(t0);
        return out;
    }

    Path np;
    for (std::size_t i = 0; i < a; ++i) {  // untouched prefix
        np.nodes.push_back(old.nodes[i]);
        np.ks.push_back(old.ks[i]);
    }
    std::vector<NodeId> det_nodes;
    std::vector<int> det_ks;
    for (NodeId v = old.nodes[static_cast<std::size_t>(best_j)]; v != src; v = parent[static_cast<std::size_t>(v)]) {
        det_nodes.push_back(v);
        det_ks.push_back(pk[static_cast<std::size_t>(v)]);
    }
    np.nodes.push_back(src);
    for (std::size_t i = det_nodes.size(); i-- > 0;) {
        np.ks.push_back(det_ks[i]);
        np.nodes.push_back(det_nodes[i]);
    }
    for (std::size_t j = static_cast<std::size_t>(best_j); j < m; ++j) {  // untouched suffix
        np.ks.push_back(old.ks[j]);
        np.nodes.push_back(old.nodes[j + 1]);
    }
    out.repaired = true;
    out.path = std::move(np);
    out.arrival = best_total;
    out.duration_ms = ms_since(t0);
    return out;
}

ReplanOutcome run_replan_case(const Graph& g, const TrafficModel& tm, const ReplanOptions& o) {
    ReplanOutcome out;
    const Disruptions none(tm.edge_count());
    const Net base{&g, &tm, &none};
    const TdResult planned = td_route(base, o.source, o.target, o.depart, EvalMode{}, true);
    if (!planned.found) return out;
    const Path& p0 = planned.path;
    out.original = p0;
    out.planned_arrival = planned.arrival;
    out.planned_travel = planned.travel_s;
    out.planned_distance = planned.distance_m;

    // Where is the vehicle when the disruption is announced?
    out.t_event = o.depart + o.event_after_s;
    std::size_t cur = 0;
    double tt = o.depart;
    while (cur < p0.edges()) {
        const double nt = base.arrive(p0.nodes[cur], p0.ks[cur], tt, EvalMode{});
        if (nt > out.t_event) break;
        tt = nt;
        ++cur;
    }
    if (p0.edges() == 0) return out;
    if (cur >= p0.edges()) cur = p0.edges() - 1;

    out.roads = o.roads;
    if (out.roads.empty()) {
        const std::size_t idx = std::min(p0.edges() - 1, cur + static_cast<std::size_t>(std::max(1, o.edges_ahead)));
        out.roads.push_back({p0.nodes[idx], p0.nodes[idx + 1]});
        if (find_edge(g, p0.nodes[idx + 1], p0.nodes[idx]) >= 0) out.roads.push_back({p0.nodes[idx + 1], p0.nodes[idx]});
    }
    Disruptions fresh(tm.edge_count());
    for (const auto& [u, v] : out.roads) {
        const int k = find_edge(g, u, v);
        if (k < 0) continue;
        const int eid = tm.first_edge(u) + k;
        if (o.closure) fresh.add_closure(eid, out.t_event, out.t_event + o.duration_s);
        else fresh.add_slowdown(eid, out.t_event, out.t_event + o.duration_s, o.factor);
    }
    const Net after{&g, &tm, &fresh};
    out.event_node = p0.nodes[cur];
    const double t_here = tt;
    Path remaining;
    remaining.nodes.assign(p0.nodes.begin() + static_cast<long>(cur), p0.nodes.end());
    remaining.ks.assign(p0.ks.begin() + static_cast<long>(cur), p0.ks.end());
    auto whole = [&](const Path& rest) {
        Path all;
        all.nodes.assign(p0.nodes.begin(), p0.nodes.begin() + static_cast<long>(cur));
        all.nodes.insert(all.nodes.end(), rest.nodes.begin(), rest.nodes.end());
        all.ks.assign(p0.ks.begin(), p0.ks.begin() + static_cast<long>(cur));
        all.ks.insert(all.ks.end(), rest.ks.begin(), rest.ks.end());
        return all;
    };
    const int rep = std::max(1, o.repeat);

    out.keep.path = p0;
    out.keep.arrival = walk_path(after, remaining, 0, t_here, EvalMode{});
    out.keep.distance_m = planned.distance_m;

    TdResult full;
    for (int i = 0; i < rep; ++i) {
        full = td_route(after, out.event_node, o.target, t_here, EvalMode{}, true);
        out.full.duration_ms += full.duration_ms / rep;
    }
    out.full.expanded = full.expanded;
    out.full.arrival = full.arrival;
    out.full.path = whole(full.path);
    out.full.distance_m = path_length_m(g, out.full.path);
    out.full.changed = full.path.nodes != remaining.nodes;

    RepairResult r;
    for (int i = 0; i < rep; ++i) {
        r = local_repair(after, fresh, remaining, t_here, o.lookback);
        out.selective.duration_ms += r.duration_ms / rep;
    }
    out.selective.expanded = r.expanded;
    out.selective.arrival = r.arrival;
    out.selective.path = whole(r.path);
    out.selective.distance_m = path_length_m(g, out.selective.path);
    out.selective.changed = r.path.nodes != remaining.nodes;
    out.selective.affected = r.affected;
    out.selective.escalated = r.escalated;
    out.ok = true;
    return out;
}

}  // namespace pathos
