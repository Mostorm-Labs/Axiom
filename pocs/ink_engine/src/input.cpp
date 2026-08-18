#include "canvas_poc02/ink_engine.h"

#include <algorithm>

namespace canvas::poc02 {
namespace {

size_t BatchBytes(const PointerSampleBatch& batch) {
  return sizeof(PointerSampleBatch) + batch.samples.size() * sizeof(PointerSample);
}

bool Compatible(const PointerSampleBatch& left, const PointerSampleBatch& right) {
  if (left.view_id != right.view_id ||
      left.viewport_revision != right.viewport_revision ||
      left.view_to_world != right.view_to_world || left.device != right.device ||
      left.samples.empty() || right.samples.empty()) {
    return false;
  }
  return left.samples.back().pointer_id == right.samples.front().pointer_id &&
         left.samples.back().sample_sequence + 1 == right.samples.front().sample_sequence &&
         left.samples.back().timestamp_us <= right.samples.front().timestamp_us;
}

}  // namespace

PointerBatchQueue::PointerBatchQueue(QueueLimits limits) : limits_(limits) {}

Status PointerBatchQueue::Enqueue(PointerSampleBatch batch, uint64_t now_us) {
  if (batch.samples.empty() || batch.samples.size() > limits_.max_batch_samples) {
    ++diagnostics_.overruns;
    return Status::kInputOverrun;
  }
  const size_t added_bytes = BatchBytes(batch);
  const bool merge = !batches_.empty() && Compatible(batches_.back(), batch) &&
                     batches_.back().samples.size() + batch.samples.size() <=
                         limits_.max_batch_samples;
  const size_t next_batches = batches_.size() + (merge ? 0U : 1U);
  const size_t next_samples = diagnostics_.samples + batch.samples.size();
  const size_t next_bytes = diagnostics_.bytes + added_bytes -
                            (merge ? sizeof(PointerSampleBatch) : 0U);
  const uint64_t oldest = batches_.empty()
                              ? batch.samples.front().timestamp_us
                              : batches_.front().samples.front().timestamp_us;
  const uint64_t oldest_age = now_us > oldest ? now_us - oldest : 0;
  if (next_batches > limits_.max_batches || next_samples > limits_.max_samples ||
      next_bytes > limits_.max_bytes || oldest_age > limits_.max_oldest_sample_age_us) {
    ++diagnostics_.overruns;
    return Status::kInputOverrun;
  }
  if (merge) {
    auto& destination = batches_.back().samples;
    destination.insert(destination.end(), batch.samples.begin(), batch.samples.end());
    ++diagnostics_.merged_batches;
  } else {
    batches_.push_back(std::move(batch));
  }
  RefreshDiagnostics(now_us);
  return Status::kOk;
}

std::optional<PointerSampleBatch> PointerBatchQueue::Pop(uint64_t now_us) {
  if (batches_.empty()) return std::nullopt;
  PointerSampleBatch batch = std::move(batches_.front());
  batches_.pop_front();
  RefreshDiagnostics(now_us);
  return batch;
}

void PointerBatchQueue::Clear() {
  batches_.clear();
  diagnostics_.batches = 0;
  diagnostics_.samples = 0;
  diagnostics_.bytes = 0;
  diagnostics_.oldest_sample_age_us = 0;
}

void PointerBatchQueue::RefreshDiagnostics(uint64_t now_us) {
  diagnostics_.batches = batches_.size();
  diagnostics_.samples = 0;
  diagnostics_.bytes = 0;
  for (const auto& batch : batches_) {
    diagnostics_.samples += batch.samples.size();
    diagnostics_.bytes += BatchBytes(batch);
  }
  if (batches_.empty()) {
    diagnostics_.oldest_sample_age_us = 0;
  } else {
    const uint64_t oldest = batches_.front().samples.front().timestamp_us;
    diagnostics_.oldest_sample_age_us = now_us > oldest ? now_us - oldest : 0;
  }
}

InputRouter::InputRouter(StrokeDocument& document, PreviewSink& preview_sink,
                         QueueLimits limits)
    : document_(document), preview_sink_(preview_sink), queue_(limits) {}

Status InputRouter::Begin(StrokeId stroke_id, PointerId pointer_id,
                          const BrushDescriptor& brush,
                          const PointerSampleBatch& first_batch) {
  if (session_ || awaiting_visible_) return Status::kInvalidState;
  auto session = std::make_unique<StrokeSession>(stroke_id, pointer_id, brush,
                                                 preview_sink_);
  const Status status = session->Begin(first_batch);
  if (status != Status::kOk) return status;
  session_ = std::move(session);
  return Status::kOk;
}

Status InputRouter::Submit(PointerSampleBatch batch, uint64_t now_us) {
  if (!session_) return Status::kInvalidState;
  const Status status = queue_.Enqueue(std::move(batch), now_us);
  if (status == Status::kInputOverrun) CancelForOverrun();
  return status;
}

Status InputRouter::Drain(uint64_t now_us) {
  if (!session_) return Status::kInvalidState;
  while (auto batch = queue_.Pop(now_us)) {
    const Status status = session_->Push(*batch);
    if (status != Status::kOk) {
      session_->Cancel();
      session_.reset();
      queue_.Clear();
      return status;
    }
  }
  return Status::kOk;
}

Status InputRouter::End(uint64_t operation_sequence,
                        AddStrokeOperation* committed_operation) {
  if (!session_ || queue_.diagnostics().batches != 0 ||
      committed_operation == nullptr) {
    return Status::kInvalidState;
  }
  Stroke stroke;
  Status status = session_->End(&stroke);
  if (status != Status::kOk) return status;
  AddStrokeOperation operation{.sequence = operation_sequence,
                               .stroke = std::move(stroke)};
  status = document_.Apply(operation);
  const StrokeId stroke_id = operation.stroke.id;
  session_.reset();
  if (status != Status::kOk) {
    preview_sink_.Cancel(stroke_id);
    return status;
  }
  status = preview_sink_.CanonicalCommitted(stroke_id, document_.revision());
  if (status != Status::kOk) return status;
  awaiting_visible_ = std::make_pair(stroke_id, document_.revision());
  *committed_operation = std::move(operation);
  return Status::kOk;
}

Status InputRouter::Cancel() {
  queue_.Clear();
  if (!session_) return Status::kInvalidState;
  const Status status = session_->Cancel();
  session_.reset();
  return status;
}

Status InputRouter::AcknowledgeCanonicalVisible(StrokeId stroke_id,
                                                uint64_t document_revision) {
  if (!awaiting_visible_ || awaiting_visible_->first != stroke_id ||
      awaiting_visible_->second != document_revision) {
    return Status::kInvalidState;
  }
  const Status status = preview_sink_.CanonicalVisible(stroke_id, document_revision);
  if (status == Status::kOk) awaiting_visible_.reset();
  return status;
}

const QueueDiagnostics& InputRouter::queue_diagnostics() const {
  return queue_.diagnostics();
}

void InputRouter::CancelForOverrun() {
  queue_.Clear();
  if (session_) {
    session_->Cancel();
    session_.reset();
  }
}

}  // namespace canvas::poc02
