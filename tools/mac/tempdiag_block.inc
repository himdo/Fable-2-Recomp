// TEMPDIAG begin: per-second GPU workload counters (temporary instrumentation).
uint64_t g_tempdiag_copy[8] = {};  // TEMPDIAG: vkCmdCopyBuffer per site
namespace {
struct TempDiag {  // TEMPDIAG
  uint64_t frames = 0, draws = 0, vs_type[16] = {}, prim[64] = {};
  uint64_t tess_prim_mode[64][4] = {};
  std::map<float, uint64_t> tmin, tmax, afac;
  float amin = 1e30f, amax = -1e30f;
  uint64_t asamples = 0, adraws = 0;
  uint64_t rp_fbo = 0, rp_transfer = 0, tess_switch = 0;
  bool last_tess = false;
  std::map<uint32_t, uint64_t> rp_cfg;
  std::map<uint64_t, uint64_t> rp_ext;
  std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
} g_td;
template <typename M>
std::string TempDiagTop(const M& m, size_t n) {  // TEMPDIAG
  std::vector<std::pair<uint64_t, typename M::key_type>> v;
  for (auto& [k, c] : m) v.emplace_back(c, k);
  std::sort(v.rbegin(), v.rend());
  std::string s;
  for (size_t i = 0; i < v.size() && i < n; ++i)
    s += fmt::format("{}:{} ", v[i].second, v[i].first);
  return s;
}
}  // namespace
// TEMPDIAG end
