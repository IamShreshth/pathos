#include "pathos/traffic.hpp"

#include <algorithm>
#include <cmath>

namespace pathos {

namespace {
constexpr double kTwoPi = 6.283185307179586;
// Share of the scenario deviation that is city-wide / regional / edge-specific.
constexpr double kWGlobal = 0.60, kWRegion = 0.50;
const double kWEdge = std::sqrt(1.0 - kWGlobal * kWGlobal - kWRegion * kWRegion);

// Excess congestion at each full hour (0 = free flow, 1 = peak), wrapped at 24 h.
constexpr double kProfile[25] = {0.00, 0.00, 0.00, 0.00, 0.00, 0.02, 0.15, 0.55, 1.00, 0.85, 0.40, 0.25, 0.30,
                                 0.30, 0.25, 0.30, 0.60, 0.95, 1.00, 0.70, 0.35, 0.15, 0.05, 0.00, 0.00};
}  // namespace

std::uint64_t mix64(std::uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

double hash_unit(std::uint64_t x) {
    return (static_cast<double>(mix64(x) >> 11) + 0.5) / 9007199254740992.0;  // 2^53
}

double hash_gauss(std::uint64_t x) {
    const double u1 = hash_unit(x), u2 = hash_unit(x ^ 0xA5A5A5A5DEADBEEFULL);
    return std::sqrt(-2.0 * std::log(u1)) * std::cos(kTwoPi * u2);
}

// ------------------------------------------------------------ Disruptions
void Disruptions::add(int eid, Span s) {
    flagged_[eid] = 1;
    spans_[eid].push_back(s);
}

double Disruptions::open_time(int eid, double t) const {
    auto it = spans_.find(eid);
    if (it == spans_.end()) return t;
    bool moved = true;
    while (moved) {
        moved = false;
        for (const Span& s : it->second) {
            if (std::isinf(s.factor) && t >= s.from && t < s.to) {
                t = s.to;
                moved = true;
            }
        }
    }
    return t;
}

double Disruptions::slowdown(int eid, double t) const {
    auto it = spans_.find(eid);
    if (it == spans_.end()) return 1.0;
    double f = 1.0;
    for (const Span& s : it->second)
        if (!std::isinf(s.factor) && t >= s.from && t < s.to) f *= s.factor;
    return f;
}

const std::vector<Span>* Disruptions::spans(int eid) const {
    auto it = spans_.find(eid);
    return it == spans_.end() ? nullptr : &it->second;
}

std::vector<int> Disruptions::edges() const {
    std::vector<int> out;
    out.reserve(spans_.size());
    for (const auto& kv : spans_) out.push_back(kv.first);
    std::sort(out.begin(), out.end());
    return out;
}

void Disruptions::merge(const Disruptions& other) {
    for (const auto& kv : other.spans_)
        for (const Span& s : kv.second) add(kv.first, s);
}

// ----------------------------------------------------------- TrafficModel
TrafficModel::TrafficModel(const Graph& g, TrafficParams p) : p_(p) {
    const std::size_t n = g.node_count();
    first_.assign(n + 1, 0);
    for (std::size_t u = 0; u < n; ++u) first_[u + 1] = first_[u] + static_cast<int>(g.neighbors(static_cast<NodeId>(u)).size());
    const std::size_t m = static_cast<std::size_t>(first_[n]);
    sens_.assign(m, 0.f);
    cv_scale_.assign(m, 1.f);
    region_.assign(m, 0);

    // City centre and radius from the bounding box.
    double s = 90, w = 180, nn = -90, e = -180;
    for (std::size_t i = 0; i < n; ++i) {
        const Node& nd = g.node(static_cast<NodeId>(i));
        s = std::min(s, nd.lat); nn = std::max(nn, nd.lat);
        w = std::min(w, nd.lon); e = std::max(e, nd.lon);
    }
    const double clat = (s + nn) / 2, clon = (w + e) / 2;
    const double radius = std::max(500.0, 0.5 * haversine_m(s, w, nn, e) * 0.5);

    for (std::size_t u = 0; u < n; ++u) {
        const auto& adj = g.neighbors(static_cast<NodeId>(u));
        for (std::size_t k = 0; k < adj.size(); ++k) {
            const Edge& ed = adj[k];
            const std::size_t eid = static_cast<std::size_t>(first_[u]) + k;
            const Node& a = g.node(static_cast<NodeId>(u));
            const Node& b = g.node(ed.to);
            const double mlat = (a.lat + b.lat) / 2, mlon = (a.lon + b.lon) / 2;
            const double centrality = std::exp(-haversine_m(mlat, mlon, clat, clon) / radius);
            const double speed = ed.time_s > 0 ? ed.length_m / ed.time_s : 0.0;
            const double road_class = speed >= 11.0 ? 1.0 : 0.7;  // faster roads feel congestion more
            const double jitter = 0.85 + 0.30 * hash_unit(mix64(p.seed ^ (eid * 2 + 1)));
            sens_[eid] = static_cast<float>((0.15 + 1.15 * centrality) * road_class * jitter);
            const auto cell_lat = static_cast<std::int64_t>(std::floor(mlat / 0.006));
            const auto cell_lon = static_cast<std::int64_t>(std::floor(mlon / 0.006));
            region_[eid] = static_cast<std::uint32_t>(mix64(static_cast<std::uint64_t>(cell_lat) * 1000003ULL +
                                                           static_cast<std::uint64_t>(cell_lon)) & 0xFFFFFFFFULL);
            // Reliability differs by district: some neighbourhoods have far more erratic travel times.
            const double rh = hash_unit(mix64(p.seed ^ (static_cast<std::uint64_t>(region_[eid]) << 1)));
            cv_scale_[eid] = static_cast<float>((0.45 + 2.6 * rh * rh) * (0.85 + 0.30 * hash_unit(mix64(p.seed ^ (eid * 2 + 7)))));
        }
    }
}

NodeId TrafficModel::tail_of(int eid) const {
    auto it = std::upper_bound(first_.begin(), first_.end(), eid);
    return static_cast<NodeId>(it - first_.begin() - 1);
}

double TrafficModel::profile(double t) {
    t = std::fmod(t, kDay);
    if (t < 0) t += kDay;
    const double h = t / 3600.0;
    const int i = static_cast<int>(h);
    const double f = h - i;
    return kProfile[i] * (1 - f) + kProfile[i + 1] * f;
}

double TrafficModel::congestion(int eid, double t) const {
    return 1.0 + p_.intensity * sens_[eid] * profile(t);
}

double TrafficModel::cv(int eid, double t) const {
    const double c = p_.base_cv * cv_scale_[eid] + p_.cv_slope * (congestion(eid, t) - 1.0);
    return std::min(c, 0.6);
}

double TrafficModel::noise(int eid, std::uint32_t scenario, double cv) const {
    if (cv <= 0) return 1.0;
    const std::uint64_t sc = mix64(p_.seed * 0x100000001B3ULL ^ (static_cast<std::uint64_t>(scenario) + 1) * 0x9E3779B97F4A7C15ULL);
    const double zg = hash_gauss(sc);
    const double zr = hash_gauss(sc ^ (static_cast<std::uint64_t>(region_[eid]) << 8) ^ 0x1234567ULL);
    const double ze = hash_gauss(sc ^ (static_cast<std::uint64_t>(eid) * 0xD6E8FEB86659FD93ULL) ^ 0x7654321ULL);
    const double z = kWGlobal * zg + kWRegion * zr + kWEdge * ze;
    return std::exp(cv * z - 0.5 * cv * cv);
}

// -------------------------------------------------------------------- Net
double Net::arrive(NodeId u, int k, double t, const EvalMode& m) const {
    const Edge& e = g->neighbors(u)[static_cast<std::size_t>(k)];
    if (m.freeflow) return t + e.time_s;
    const int eid = tm->first_edge(u) + k;
    double slow = 1.0;
    if (dis && dis->flagged(eid)) {
        t = dis->open_time(eid, t);
        if (std::isinf(t)) return kInf;
        slow = dis->slowdown(eid, t);
    }
    double tau = e.time_s * tm->congestion(eid, t) * slow;
    if (m.scenario >= 0 || m.kappa > 0) {
        const double c = tm->cv(eid, t);
        if (m.scenario >= 0) tau *= tm->noise(eid, static_cast<std::uint32_t>(m.scenario), c);
        else tau *= 1.0 + m.kappa * c;
    }
    return t + tau;
}

}  // namespace pathos
