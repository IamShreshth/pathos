#include "pathos/fleet.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <numeric>
#include <random>

namespace pathos {

namespace {
using Clock = std::chrono::steady_clock;
double ms_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}
}  // namespace

// ------------------------------------------------------------------ matrix
void TdMatrix::build(const Net& net, const std::vector<NodeId>& nodes, double t0, double horizon_s, int max_buckets,
                     const EvalMode& mode) {
    const auto start = Clock::now();
    k_ = static_cast<int>(nodes.size());
    const int need = static_cast<int>(std::ceil(std::max(horizon_s, 1800.0) / 1800.0)) + 1;
    b_ = std::max(2, std::min(max_buckets, need));
    t0_ = t0;
    step_ = std::max(horizon_s, 1800.0) / (b_ - 1);
    const std::size_t kk = static_cast<std::size_t>(k_) * static_cast<std::size_t>(k_);
    t_.assign(kk * static_cast<std::size_t>(b_), kInf);
    d_.assign(kk * static_cast<std::size_t>(b_), kInf);
    searches = 0;

    const std::size_t n = net.g->node_count();
    TdSearch s;
    TdQuery q;
    q.targets = &nodes;
    q.mode = mode;
    std::vector<double> dd(n);
    std::vector<NodeId> chain;
    for (int b = 0; b < b_; ++b) {
        const double depart = t0_ + b * step_;
        for (int i = 0; i < k_; ++i) {
            q.source = nodes[static_cast<std::size_t>(i)];
            q.depart = depart;
            td_search(net, q, s);
            ++searches;
            std::fill(dd.begin(), dd.end(), -1.0);
            dd[static_cast<std::size_t>(q.source)] = 0.0;
            for (int j = 0; j < k_; ++j) {
                const NodeId tgt = nodes[static_cast<std::size_t>(j)];
                const double arr = s.arr[static_cast<std::size_t>(tgt)];
                const std::size_t at = (static_cast<std::size_t>(b) * static_cast<std::size_t>(k_) + static_cast<std::size_t>(i)) *
                                           static_cast<std::size_t>(k_) + static_cast<std::size_t>(j);
                if (i == j) { t_[at] = 0; d_[at] = 0; continue; }
                if (arr == kInf) continue;
                chain.clear();
                NodeId v = tgt;
                while (dd[static_cast<std::size_t>(v)] < 0) {
                    chain.push_back(v);
                    v = s.parent[static_cast<std::size_t>(v)];
                }
                while (!chain.empty()) {
                    const NodeId c = chain.back();
                    chain.pop_back();
                    const NodeId p = s.parent[static_cast<std::size_t>(c)];
                    dd[static_cast<std::size_t>(c)] = dd[static_cast<std::size_t>(p)] +
                        net.g->neighbors(p)[static_cast<std::size_t>(s.pk[static_cast<std::size_t>(c)])].length_m;
                }
                t_[at] = arr - depart;
                d_[at] = dd[static_cast<std::size_t>(tgt)];
            }
        }
    }
    build_ms = ms_since(start);
}

double TdMatrix::interp(const std::vector<double>& a, int i, int j, double t) const {
    if (i == j) return 0.0;
    const std::size_t kk = static_cast<std::size_t>(k_) * static_cast<std::size_t>(k_);
    const std::size_t off = static_cast<std::size_t>(i) * static_cast<std::size_t>(k_) + static_cast<std::size_t>(j);
    double pos = (t - t0_) / step_;
    if (pos <= 0) return a[off];
    if (pos >= b_ - 1) return a[static_cast<std::size_t>(b_ - 1) * kk + off];
    const int lo = static_cast<int>(pos);
    const double f = pos - lo;
    const double x = a[static_cast<std::size_t>(lo) * kk + off], y = a[static_cast<std::size_t>(lo + 1) * kk + off];
    if (x == kInf || y == kInf) return kInf;
    return x + (y - x) * f;
}
double TdMatrix::time(int i, int j, double t) const { return interp(t_, i, j, t); }
double TdMatrix::dist(int i, int j, double t) const { return interp(d_, i, j, t); }

// ---------------------------------------------------------------- problem
int Solution::unassigned() const {
    int c = 0;
    for (int v : veh_of) c += v < 0;
    return c;
}

Problem make_problem(const std::vector<Vehicle>& vehicles, const std::vector<Request>& requests, double now) {
    Problem P;
    P.now = now;
    for (const Vehicle& v : vehicles) {
        SubVehicle s;
        s.vid = v.id;
        s.pos = v.depot;
        s.depot = v.depot;
        s.avail = std::max(now, v.shift_start);
        s.cap_total = s.cap_left = v.capacity;
        s.shift_end = v.shift_end;
        s.pays_fixed = true;
        P.veh.push_back(s);
    }
    for (const Request& r : requests) P.req.push_back({r.id, r.node, r.demand, r.tw_start, r.tw_end, r.service_s});
    P.warm.assign(P.veh.size(), {});
    P.prev_veh.assign(P.req.size(), -1);
    P.prev_rank.assign(P.req.size(), -1);
    return P;
}

void build_matrix(const Net& net, Problem& P, const FleetParams& fp, TdMatrix& M) {
    std::vector<NodeId> nodes;
    for (const auto& v : P.veh) { nodes.push_back(v.pos); nodes.push_back(v.depot); }
    for (const auto& r : P.req) nodes.push_back(r.node);
    std::sort(nodes.begin(), nodes.end());
    nodes.erase(std::unique(nodes.begin(), nodes.end()), nodes.end());
    auto idx = [&](NodeId n) { return static_cast<int>(std::lower_bound(nodes.begin(), nodes.end(), n) - nodes.begin()); };
    P.pos_idx.clear(); P.depot_idx.clear(); P.req_idx.clear();
    double end = P.now;
    for (const auto& v : P.veh) {
        P.pos_idx.push_back(idx(v.pos));
        P.depot_idx.push_back(idx(v.depot));
        end = std::max(end, v.shift_end);
    }
    for (const auto& r : P.req) P.req_idx.push_back(idx(r.node));
    EvalMode mode;
    mode.kappa = fp.kappa;
    M.build(net, nodes, P.now, std::min(end - P.now, hours(6)), 9, mode);
}

RouteEval eval_route(const Problem& P, const TdMatrix& M, const FleetParams& fp, int v, const std::vector<int>& seq,
                     std::vector<double>* arrivals) {
    RouteEval ev;
    const SubVehicle& V = P.veh[static_cast<std::size_t>(v)];
    if (arrivals) arrivals->clear();
    ev.finish = V.avail;
    if (seq.empty()) { ev.ok = true; return ev; }
    double t = V.avail;
    int cur = P.pos_idx[static_cast<std::size_t>(v)];
    double load = V.cap_total - V.cap_left;
    for (int r : seq) {
        const SubRequest& R = P.req[static_cast<std::size_t>(r)];
        const int to = P.req_idx[static_cast<std::size_t>(r)];
        const double tt = M.time(cur, to, t);
        if (tt == kInf) return ev;
        const double dd = M.dist(cur, to, t);
        const double arr = t + tt;
        ev.energy += dd / 1000.0 * fp.kwh_per_km * (1.0 + 0.6 * load / std::max(1.0, V.cap_total));
        load += R.demand;
        if (load > V.cap_total + 1e-9) return ev;
        const double late = std::max(0.0, arr - R.tw_end);
        if (late > fp.max_late_s + 1e-9) return ev;
        ev.late += late;
        ev.travel += tt;
        ev.dist += dd;
        const double start = std::max(arr, R.tw_start);
        ev.wait += start - arr;
        const double slack = R.tw_end - arr;
        ev.risk += std::min(1.0, std::max(0.0, 1.0 - slack / fp.risk_margin_s));
        if (arrivals) arrivals->push_back(arr);
        t = start + R.service;
        cur = to;
    }
    const int dep = P.depot_idx[static_cast<std::size_t>(v)];
    const double tt = M.time(cur, dep, t);
    if (tt == kInf) return ev;
    const double dd = M.dist(cur, dep, t);
    ev.energy += dd / 1000.0 * fp.kwh_per_km * (1.0 + 0.6 * load / std::max(1.0, V.cap_total));
    ev.travel += tt;
    ev.dist += dd;
    ev.finish = t + tt;
    if (arrivals) arrivals->push_back(ev.finish);
    if (ev.finish > V.shift_end + 1e-9) return ev;
    ev.cost = fp.w_time * ev.travel + fp.w_dist * ev.dist + fp.w_energy * ev.energy + fp.w_late * ev.late +
              fp.w_risk * ev.risk + fp.w_wait * ev.wait + (V.pays_fixed ? fp.vehicle_fixed : 0.0);
    ev.ok = true;
    return ev;
}

double schedule_disruption(const Problem& P, const std::vector<std::vector<int>>& routes) {
    double d = 0;
    std::vector<std::pair<int, int>> stay;  // (previous rank, position among stayers)
    for (std::size_t k = 0; k < routes.size(); ++k) {
        stay.clear();
        int pos = 0;
        for (int r : routes[k]) {
            const int pv = P.prev_veh[static_cast<std::size_t>(r)];
            if (pv < 0) continue;  // new request: nothing to disturb
            if (pv != static_cast<int>(k)) { d += 1.0; continue; }
            stay.push_back({P.prev_rank[static_cast<std::size_t>(r)], pos++});
        }
        std::sort(stay.begin(), stay.end());
        for (std::size_t i = 0; i < stay.size(); ++i)
            if (stay[i].second != static_cast<int>(i)) d += 0.5;
    }
    return d;
}

double objective(const Problem& P, const FleetParams& fp, const Solution& s) {
    double c = 0;
    for (const RouteEval& e : s.ev) c += e.cost;
    c += fp.unassigned_penalty * s.unassigned();
    if (fp.beta > 0) c += fp.beta * schedule_disruption(P, s.routes);
    return c;
}

Solution make_solution(const Problem& P, const TdMatrix& M, const FleetParams& fp,
                       const std::vector<std::vector<int>>& routes) {
    Solution s;
    s.routes = routes;
    s.routes.resize(P.veh.size());
    s.veh_of.assign(P.req.size(), -1);
    for (std::size_t v = 0; v < s.routes.size(); ++v) {
        s.ev.push_back(eval_route(P, M, fp, static_cast<int>(v), s.routes[v]));
        for (int r : s.routes[v]) s.veh_of[static_cast<std::size_t>(r)] = static_cast<int>(v);
    }
    s.disruption = schedule_disruption(P, s.routes);
    s.cost = objective(P, fp, s);
    for (const RouteEval& e : s.ev)
        if (!e.ok) s.cost = kInf;
    return s;
}

// ------------------------------------------------------------------ ALNS
namespace {

struct Ins {
    double delta = kInf;
    int pos = -1;
};

class Planner {
public:
    Planner(const Problem& P, const TdMatrix& M, const FleetParams& fp, std::uint64_t seed)
        : P_(P), M_(M), fp_(fp), rng_(seed), nv_(static_cast<int>(P.veh.size())), nr_(static_cast<int>(P.req.size())) {}

    Solution start(PlanMode mode) {
        Solution s;
        s.routes.assign(static_cast<std::size_t>(nv_), {});
        s.ev.assign(static_cast<std::size_t>(nv_), RouteEval{});
        s.veh_of.assign(static_cast<std::size_t>(nr_), -1);
        for (int v = 0; v < nv_; ++v) set_route(s, v, {});
        if (mode != PlanMode::Scratch) {
            for (int v = 0; v < nv_ && v < static_cast<int>(P_.warm.size()); ++v) {
                std::vector<int> seq;
                for (int r : P_.warm[static_cast<std::size_t>(v)]) {
                    seq.push_back(r);
                    if (!eval_route(P_, M_, fp_, v, seq).ok) seq.pop_back();  // no longer feasible: re-planned below
                }
                set_route(s, v, seq);
            }
        }
        std::vector<int> pool;
        for (int r = 0; r < nr_; ++r)
            if (s.veh_of[static_cast<std::size_t>(r)] < 0) pool.push_back(r);
        repair(s, pool, mode != PlanMode::InsertOnly);
        refresh(s);
        return s;
    }

    Solution run(Solution init, int iters, double budget_ms, PlanStats* st) {
        const auto t0 = Clock::now();
        Solution cur = init, best = init;
        refresh(cur);
        refresh(best);
        if (st) st->initial_cost = cur.cost;
        if (nr_ == 0 || iters <= 0) return best;
        double route_cost = 0;
        for (const RouteEval& e : cur.ev) route_cost += e.cost;
        const double T0 = std::max(1.0, 0.05 * (route_cost + 1.0) / 0.6931);
        const double Tf = T0 * 0.005;
        std::vector<double> wd(4, 1.0), wr(2, 1.0), sd(4, 0.0), sr(2, 0.0);
        std::vector<int> ud(4, 0), ur(2, 0);
        std::uniform_real_distribution<double> U(0.0, 1.0);

        int it = 0;
        for (; it < iters; ++it) {
            if (budget_ms > 0 && (it & 15) == 0 && ms_since(t0) > budget_ms) break;
            const double temp = T0 * std::pow(Tf / T0, static_cast<double>(it) / std::max(1, iters));
            Solution cand = cur;
            std::vector<int> assigned;
            for (int r = 0; r < nr_; ++r)
                if (cand.veh_of[static_cast<std::size_t>(r)] >= 0) assigned.push_back(r);
            if (!assigned.empty()) {
                const int n = static_cast<int>(assigned.size());
                const int qmin = std::min(n, std::max(2, static_cast<int>(0.08 * n)));
                const int qmax = std::min(n, std::max(qmin, std::min(25, static_cast<int>(0.35 * n))));
                const int q = std::uniform_int_distribution<int>(qmin, qmax)(rng_);
                const int d = roulette(wd);
                switch (d) {
                    case 0: destroy_random(cand, assigned, q); break;
                    case 1: destroy_worst(cand, assigned, q); break;
                    case 2: destroy_shaw(cand, assigned, q); break;
                    default: destroy_route(cand, q); break;
                }
                std::vector<int> pool;
                for (int r = 0; r < nr_; ++r)
                    if (cand.veh_of[static_cast<std::size_t>(r)] < 0) pool.push_back(r);
                const int rp = roulette(wr);
                repair(cand, pool, rp == 1);
                refresh(cand);

                double score = 0;
                if (cand.cost < best.cost - 1e-9) {
                    best = cand;
                    cur = cand;
                    score = 33;
                    if (st) ++st->improved;
                } else if (cand.cost < cur.cost - 1e-9) {
                    cur = cand;
                    score = 9;
                    if (st) ++st->accepted;
                } else if (cand.cost < kInf && U(rng_) < std::exp(-(cand.cost - cur.cost) / temp)) {
                    cur = cand;
                    score = 13;
                    if (st) ++st->accepted;
                }
                sd[static_cast<std::size_t>(d)] += score; ++ud[static_cast<std::size_t>(d)];
                sr[static_cast<std::size_t>(rp)] += score; ++ur[static_cast<std::size_t>(rp)];
            }
            if ((it + 1) % 40 == 0) {
                for (int i = 0; i < 4; ++i) {
                    if (ud[static_cast<std::size_t>(i)]) wd[static_cast<std::size_t>(i)] = std::max(0.05, 0.8 * wd[static_cast<std::size_t>(i)] + 0.2 * sd[static_cast<std::size_t>(i)] / ud[static_cast<std::size_t>(i)]);
                    sd[static_cast<std::size_t>(i)] = 0; ud[static_cast<std::size_t>(i)] = 0;
                }
                for (int i = 0; i < 2; ++i) {
                    if (ur[static_cast<std::size_t>(i)]) wr[static_cast<std::size_t>(i)] = std::max(0.05, 0.8 * wr[static_cast<std::size_t>(i)] + 0.2 * sr[static_cast<std::size_t>(i)] / ur[static_cast<std::size_t>(i)]);
                    sr[static_cast<std::size_t>(i)] = 0; ur[static_cast<std::size_t>(i)] = 0;
                }
            }
        }
        if (st) st->iterations = it;
        return best;
    }

private:
    int roulette(const std::vector<double>& w) {
        double tot = std::accumulate(w.begin(), w.end(), 0.0);
        double x = std::uniform_real_distribution<double>(0.0, tot)(rng_);
        for (std::size_t i = 0; i < w.size(); ++i) {
            x -= w[i];
            if (x <= 0) return static_cast<int>(i);
        }
        return static_cast<int>(w.size()) - 1;
    }

    void set_route(Solution& s, int v, std::vector<int> seq) {
        for (int r : s.routes[static_cast<std::size_t>(v)]) s.veh_of[static_cast<std::size_t>(r)] = -1;
        s.routes[static_cast<std::size_t>(v)] = std::move(seq);
        s.ev[static_cast<std::size_t>(v)] = eval_route(P_, M_, fp_, v, s.routes[static_cast<std::size_t>(v)]);
        for (int r : s.routes[static_cast<std::size_t>(v)]) s.veh_of[static_cast<std::size_t>(r)] = v;
    }

    void refresh(Solution& s) {
        s.disruption = fp_.beta > 0 ? schedule_disruption(P_, s.routes) : 0.0;
        s.cost = objective(P_, fp_, s);
    }

    Ins best_in_route(const Solution& s, int r, int v) {
        Ins best;
        const std::vector<int>& seq = s.routes[static_cast<std::size_t>(v)];
        const SubVehicle& V = P_.veh[static_cast<std::size_t>(v)];
        double load = 0;
        for (int q : seq) load += P_.req[static_cast<std::size_t>(q)].demand;
        if (load + P_.req[static_cast<std::size_t>(r)].demand > V.cap_left + 1e-9) return best;
        const double base = s.ev[static_cast<std::size_t>(v)].cost;
        const int pv = P_.prev_veh[static_cast<std::size_t>(r)];
        const double move = (pv >= 0 && pv != v) ? fp_.beta : 0.0;
        tmp_.assign(seq.begin(), seq.end());
        tmp_.insert(tmp_.begin(), r);
        for (std::size_t pos = 0; pos <= seq.size(); ++pos) {
            if (pos > 0) std::swap(tmp_[pos - 1], tmp_[pos]);  // moves r one slot right
            const RouteEval e = eval_route(P_, M_, fp_, v, tmp_);
            if (!e.ok) continue;
            const double delta = e.cost - base + move;
            if (delta < best.delta) { best.delta = delta; best.pos = static_cast<int>(pos); }
        }
        return best;
    }

public:
    void repair(Solution& s, const std::vector<int>& pool, bool regret) {
        const std::size_t np = pool.size();
        std::vector<std::vector<Ins>> cache(np, std::vector<Ins>(static_cast<std::size_t>(nv_)));
        std::vector<std::vector<char>> valid(np, std::vector<char>(static_cast<std::size_t>(nv_), 0));
        std::vector<char> alive(np, 1);
        for (;;) {
            int pick = -1, pv = -1, ppos = -1;
            double pkey = -kInf, pbest = kInf;
            for (std::size_t i = 0; i < np; ++i) {
                if (!alive[i]) continue;
                double b1 = kInf, b2 = kInf;
                int bv = -1;
                for (int v = 0; v < nv_; ++v) {
                    if (!valid[i][static_cast<std::size_t>(v)]) {
                        cache[i][static_cast<std::size_t>(v)] = best_in_route(s, pool[i], v);
                        valid[i][static_cast<std::size_t>(v)] = 1;
                    }
                    const double d = cache[i][static_cast<std::size_t>(v)].delta;
                    if (d < b1) { b2 = b1; b1 = d; bv = v; }
                    else if (d < b2) b2 = d;
                }
                if (bv < 0) { alive[i] = 0; continue; }
                const double key = regret ? (b2 == kInf ? 1e9 : b2 - b1) : -b1;
                if (key > pkey || (key == pkey && b1 < pbest)) {
                    pkey = key; pbest = b1; pick = static_cast<int>(i); pv = bv;
                    ppos = cache[i][static_cast<std::size_t>(bv)].pos;
                }
            }
            if (pick < 0) break;
            std::vector<int> seq = s.routes[static_cast<std::size_t>(pv)];
            seq.insert(seq.begin() + ppos, pool[static_cast<std::size_t>(pick)]);
            set_route(s, pv, std::move(seq));
            alive[static_cast<std::size_t>(pick)] = 0;
            for (std::size_t i = 0; i < np; ++i) valid[i][static_cast<std::size_t>(pv)] = 0;
        }
    }

private:
    void remove_request(Solution& s, int r) {
        const int v = s.veh_of[static_cast<std::size_t>(r)];
        if (v < 0) return;
        std::vector<int> seq = s.routes[static_cast<std::size_t>(v)];
        seq.erase(std::find(seq.begin(), seq.end(), r));
        set_route(s, v, std::move(seq));
    }

    void destroy_random(Solution& s, std::vector<int> assigned, int q) {
        std::shuffle(assigned.begin(), assigned.end(), rng_);
        for (int i = 0; i < q && i < static_cast<int>(assigned.size()); ++i) remove_request(s, assigned[static_cast<std::size_t>(i)]);
    }

    void destroy_worst(Solution& s, const std::vector<int>& assigned, int q) {
        std::vector<std::pair<double, int>> sav;
        for (int r : assigned) {
            const int v = s.veh_of[static_cast<std::size_t>(r)];
            std::vector<int> seq = s.routes[static_cast<std::size_t>(v)];
            seq.erase(std::find(seq.begin(), seq.end(), r));
            const RouteEval e = eval_route(P_, M_, fp_, v, seq);
            sav.push_back({s.ev[static_cast<std::size_t>(v)].cost - (e.ok ? e.cost : 0.0), r});
        }
        std::sort(sav.begin(), sav.end(), std::greater<>());
        std::uniform_real_distribution<double> U(0.0, 1.0);
        for (int i = 0; i < q && !sav.empty(); ++i) {
            const std::size_t k = std::min(sav.size() - 1, static_cast<std::size_t>(sav.size() * std::pow(U(rng_), 3.0)));
            remove_request(s, sav[k].second);
            sav.erase(sav.begin() + static_cast<long>(k));
        }
    }

    void destroy_shaw(Solution& s, const std::vector<int>& assigned, int q) {
        // Remove requests that are close in space, time window and demand to a random seed request.
        const int seed = assigned[std::uniform_int_distribution<std::size_t>(0, assigned.size() - 1)(rng_)];
        auto rel = [&](int a, int b) {
            const SubRequest& A = P_.req[static_cast<std::size_t>(a)];
            const SubRequest& B = P_.req[static_cast<std::size_t>(b)];
            const double t = M_.time(P_.req_idx[static_cast<std::size_t>(a)], P_.req_idx[static_cast<std::size_t>(b)], P_.now);
            return std::min(t, 7200.0) / 600.0 + std::fabs(A.tw_start - B.tw_start) / 1800.0 + std::fabs(A.demand - B.demand);
        };
        std::vector<std::pair<double, int>> order;
        for (int r : assigned) order.push_back({r == seed ? -1.0 : rel(seed, r), r});
        std::sort(order.begin(), order.end());
        std::uniform_real_distribution<double> U(0.0, 1.0);
        for (int i = 0; i < q && !order.empty(); ++i) {
            const std::size_t k = std::min(order.size() - 1, static_cast<std::size_t>(order.size() * std::pow(U(rng_), 4.0)));
            remove_request(s, order[k].second);
            order.erase(order.begin() + static_cast<long>(k));
        }
    }

    void destroy_route(Solution& s, int q) {
        std::vector<int> cands;
        for (int v = 0; v < nv_; ++v)
            if (!s.routes[static_cast<std::size_t>(v)].empty()) cands.push_back(v);
        if (cands.empty()) return;
        std::sort(cands.begin(), cands.end(), [&](int a, int b) {
            return s.routes[static_cast<std::size_t>(a)].size() < s.routes[static_cast<std::size_t>(b)].size();
        });
        const std::size_t k = static_cast<std::size_t>(cands.size() * std::pow(std::uniform_real_distribution<double>(0, 1)(rng_), 2.0));
        const int v = cands[std::min(k, cands.size() - 1)];
        std::vector<int> seq = s.routes[static_cast<std::size_t>(v)];
        std::shuffle(seq.begin(), seq.end(), rng_);
        for (int i = 0; i < std::max(q, 1) && i < static_cast<int>(seq.size()); ++i) remove_request(s, seq[static_cast<std::size_t>(i)]);
    }

    const Problem& P_;
    const TdMatrix& M_;
    const FleetParams& fp_;
    std::mt19937_64 rng_;
    int nv_, nr_;
    std::vector<int> tmp_;
};

}  // namespace

Solution plan_problem(const Net& net, Problem& P, const FleetParams& fp, PlanMode mode, int iterations,
                      PlanStats* stats, double time_budget_ms) {
    TdMatrix M;
    build_matrix(net, P, fp, M);
    const auto t0 = Clock::now();
    PlanStats local;
    PlanStats* st = stats ? stats : &local;
    st->matrix_ms = M.build_ms;
    st->matrix_searches = M.searches;
    st->matrix_nodes = M.size();
    Planner planner(P, M, fp, fp.seed);
    Solution s = planner.start(mode);
    if (mode != PlanMode::InsertOnly) s = planner.run(s, iterations, time_budget_ms, st);
    else st->initial_cost = s.cost;
    st->solve_ms = ms_since(t0);
    return s;
}

Solution brute_force(const Problem& P, const TdMatrix& M, const FleetParams& fp) {
    const int n = static_cast<int>(P.req.size());
    const int nv = static_cast<int>(P.veh.size());
    std::vector<int> perm(static_cast<std::size_t>(n));
    std::iota(perm.begin(), perm.end(), 0);
    Solution best;
    best.cost = kInf;
    std::vector<std::vector<int>> routes(static_cast<std::size_t>(nv));
    // Any assignment + ordering is a permutation cut into nv consecutive segments.
    std::vector<int> cuts(static_cast<std::size_t>(nv + 1), 0);
    do {
        std::function<void(int, int)> rec = [&](int v, int from) {
            if (v == nv - 1) {
                routes[static_cast<std::size_t>(v)].assign(perm.begin() + from, perm.end());
                Solution s = make_solution(P, M, fp, routes);
                if (s.cost < best.cost) best = s;
                return;
            }
            for (int to = from; to <= n; ++to) {
                routes[static_cast<std::size_t>(v)].assign(perm.begin() + from, perm.begin() + to);
                rec(v + 1, to);
            }
        };
        if (nv > 0) rec(0, 0);
    } while (std::next_permutation(perm.begin(), perm.end()));
    return best;
}

}  // namespace pathos
