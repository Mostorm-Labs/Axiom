#ifndef CANVAS_POC_CONFORMANCE_H_
#define CANVAS_POC_CONFORMANCE_H_

#include <cstdint>
#include <string>

namespace canvas::poc01 {

struct CoreConformanceResult {
  std::string corpus_digest;
  std::string replay_digest;
  uint32_t case_count = 0;
  uint64_t replay_revision = 0;
  uint64_t replay_sequence = 0;
  bool passed = false;
  std::string failure;
};

// Runs the architecture-level POC-01 numeric and Operation replay corpus.
// Observed values always contribute to corpus_digest. A contract mismatch is
// returned through passed/failure so Release WASM does not turn diagnostics
// into an opaque exception abort.
CoreConformanceResult RunCoreConformance();
std::string CoreConformanceJsonFields(const CoreConformanceResult& result);

}  // namespace canvas::poc01

#endif  // CANVAS_POC_CONFORMANCE_H_
