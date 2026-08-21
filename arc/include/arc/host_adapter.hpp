#ifndef ARC_HOST_ADAPTER_HPP_
#define ARC_HOST_ADAPTER_HPP_

#include <cstdint>
#include <memory>

#include "arc/arc.hpp"

namespace arc {

// Platform Host composition root for one Arc preview target and one native
// input source. Native window/view/GPU/event types stay behind the two owned
// interfaces and never enter Arc::Protocol.
//
// HostAdapter intentionally does not own an Axiom Document, StrokeSession or
// canonical render target. Presentation failures therefore remain isolated in
// Bridge while input-source failures are reported only to PointerSampleSink.
class HostAdapter final : private PointerSampleSink {
 public:
  HostAdapter(std::unique_ptr<PreviewBackend> primary,
              std::unique_ptr<PreviewBackend> fallback,
              std::unique_ptr<InputSource> input_source,
              BridgeLimits limits = {});
  ~HostAdapter();
  HostAdapter(const HostAdapter&) = delete;
  HostAdapter& operator=(const HostAdapter&) = delete;

  Status AttachTarget(const arc_preview_target_v0& target);
  Status ResizeTarget(const arc_preview_target_v0& target);
  Status DetachTarget(uint64_t target_generation);
  void SurfaceLost(uint64_t target_generation);

  Status StartInput(PointerSampleSink& sink);
  Status StopInput();
  Status SubmitPointerBatch(const arc_pointer_sample_batch_v0& batch);
  void NotifyInputLost(Status reason);

  // Canonical presentation receipts are accepted only for the currently
  // attached generation. Bridge performs the handoff-token/revision/evidence
  // checks before retiring Preview geometry.
  Status CanonicalVisible(const arc_canonical_visible_v0& visible);

  [[nodiscard]] bool target_attached() const;
  [[nodiscard]] bool input_running() const;
  [[nodiscard]] uint64_t target_generation() const;
  [[nodiscard]] bool TakeCanonicalRedrawRequest();
  [[nodiscard]] Bridge& bridge();
  [[nodiscard]] const Bridge& bridge() const;

 private:
  class Impl;
  Status Push(const arc_pointer_sample_batch_v0& batch) override;
  void SourceLost(uint64_t device_id, Status reason) override;

  std::unique_ptr<Impl> impl_;
};

}  // namespace arc

#endif  // ARC_HOST_ADAPTER_HPP_
