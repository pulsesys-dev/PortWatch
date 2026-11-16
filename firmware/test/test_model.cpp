// Host test: runs the firmware's feed parser on full-size AnyPS5 JSON files.
//   g++ -std=c++17 -O1 -I<ArduinoJson/src> -I../PortWatch test_model.cpp -o test_model
//   ./test_model progress.json history.json
#include <cstdio>
#include <fstream>
#include <string>
#include <cstring>
#include <ArduinoJson.h>
#if !defined(__GLIBC__) || __GLIBC__ < 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ < 38)
static size_t strlcpy(char *d, const char *s, size_t n) { size_t l = strlen(s); if (n) { size_t k = l < n - 1 ? l : n - 1; memcpy(d, s, k); d[k] = 0; } return l; }
#endif
#include "model.h"

struct PeakAlloc : ArduinoJson::Allocator {
  size_t cur = 0, peak = 0;
  void *allocate(size_t n) override { auto p = (size_t *)malloc(n + 16); *p = n; cur += n; peak = std::max(peak, cur); return (char *)p + 16; }
  void deallocate(void *p) override { if (!p) return; auto h = (size_t *)((char *)p - 16); cur -= *h; free(h); }
  void *reallocate(void *p, size_t n) override { if (!p) return allocate(n); auto h = (size_t *)((char *)p - 16); cur -= *h; h = (size_t *)realloc(h, n + 16); *h = n; cur += n; peak = std::max(peak, cur); return (char *)h + 16; }
};
int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL: %s (line %d)\n", #c, __LINE__); fails++; } } while (0)

int main(int argc, char **argv) {
  if (argc < 3) { puts("usage: test_model progress.json history.json"); return 2; }
  PeakAlloc pa;
  Model m;
  std::ifstream f(argv[1], std::ios::binary);
  const char *err = parseProgress(f, &m, &pa);
  printf("progress: err=%s done=%u total=%u pct=%.2f libs=%u complete=%u/%u\n", err ? err : "none", m.done, m.total, m.percent, m.nLibs, m.libsComplete(), m.libsWithFns());
  printf("shader: legacy=%d forms %u/%u translated %u dpp %u/%u throws %u areas %u formats %u/%u/%u of %u pr=%d %u %.2f\n",
         m.legacy, m.fDecoded, m.fTotal, m.fTranslated, m.dppDecoded, m.dppTotal, m.throws, m.nAreas, m.fmtSampled, m.fmtBuffer, m.fmtHost, m.fmtTotal, m.hasPr, m.prDone, m.prPercent);
  for (int i = 0; i < m.nAreas; i++) printf("  area %s %u\n", m.areas[i].name, m.areas[i].count);
  printf("JSON peak while parsing progress: %zu bytes\n", pa.peak);
  CHECK(!err); CHECK(m.nLibs > 0);
  uint32_t sum = 0, tot = 0; for (int i = 0; i < m.nLibs; i++) { sum += m.libs[i].done; tot += m.libs[i].done + m.libs[i].todo; }
  CHECK(sum == m.done); CHECK(tot == m.total);
  bool foundKernel = false; for (int i = 0; i < m.nLibs; i++) if (!strcmp(m.libs[i].name, "libkernel")) foundKernel = true;
  CHECK(foundKernel);
  bool foundAgc = false; for (int i = 0; i < m.nLibs; i++) if (!strcmp(m.libs[i].name, "Agc")) foundAgc = true;   // libSce prefix stripped
  CHECK(foundAgc);
  PeakAlloc ph;
  std::ifstream fh(argv[2], std::ios::binary);
  CHECK(parseHistory(fh, &m, &ph));
  printf("history: %u points, first day %d (%u/%u) last day %d (%u/%u), JSON peak %zu bytes\n", m.nHist, m.hist[0].day, m.hist[0].done, m.hist[0].total,
         m.hist[m.nHist - 1].day, m.hist[m.nHist - 1].done, m.hist[m.nHist - 1].total, ph.peak);
  CHECK(m.nHist > 1);
  CHECK(m.hist[m.nHist - 1].day == daysFromCivil(2026, 11, 13));
  CHECK(parseHttpDate(std::string("Thu, 13 Nov 2026 01:02:03 GMT")) == (time_t)daysFromCivil(2026, 11, 13) * 86400 + 3723);
  // truncated file must fail cleanly
  std::string all; { std::ifstream f2(argv[1], std::ios::binary); all.assign(std::istreambuf_iterator<char>(f2), {}); }
  std::string cut = all.substr(0, all.size() / 3);
  Model bad; const char *e2 = parseProgress(cut, &bad);
  printf("truncated feed -> %s\n", e2 ? e2 : "(no error!)"); CHECK(e2 != nullptr);
  // legacy feed without shader_source
  std::string legj("{\"libraries\":{\"done\":3,\"total\":4,\"percent\":75,\"groups\":[{\"name\":\"libSceFoo\",\"done\":3,\"todo\":1,\"done_names\":[\"a\",\"b\",\"c\"]}]},\"shaders\":{\"done\":5,\"total\":10,\"percent\":50,\"groups\":[]}}");
  Model leg; const char *e3 = parseProgress(legj, &leg);
  CHECK(!e3); CHECK(leg.legacy); CHECK(leg.fDecoded == 5); CHECK(!strcmp(leg.libs[0].name, "Foo"));
  printf(fails ? "%d FAILED\n" : "ALL PASSED\n", fails);
  return fails != 0;
}
