#include "canvas_poc02/ink_engine.h"

#include <algorithm>
#include <exception>
#include <limits>
#include <utility>

#include "foundation.h"

namespace canvas::poc02 {
namespace {

constexpr uint64_t kResampleIntervalUs = 4000;

struct CandidateState {
  Stroke stroke;
  std::vector<PreviewPrimitive> preview_confirmed;
  std::optional<CanonicalSample> prior_resampled;
  std::optional<CanonicalSample> prior_smoothed;
  std::optional<Vec2> prior_dab_position;
  uint64_t prior_input_timestamp = 0;
  uint64_t next_sample_sequence = 0;
  bool has_input = false;
  size_t incremental_work_count = 0;
};

float MapPressure(float pressure) {
  return internal::CanonicalFloat(static_cast<double>(pressure) * pressure);
}

CanonicalSample Interpolate(const CanonicalSample& left,
                            const CanonicalSample& right,
                            uint64_t numerator, uint64_t denominator) {
  const double t = static_cast<double>(numerator) /
                   static_cast<double>(denominator);
  const auto interpolate = [t](float a, float b) {
    return internal::CanonicalFloat(static_cast<double>(a) +
                                    (static_cast<double>(b) - a) * t);
  };
  return CanonicalSample{
      .position = {interpolate(left.position.x, right.position.x),
                   interpolate(left.position.y, right.position.y)},
      .pressure = interpolate(left.pressure, right.pressure),
      .tilt = {interpolate(left.tilt.x, right.tilt.x),
               interpolate(left.tilt.y, right.tilt.y)},
      .timestamp_us = left.timestamp_us +
          ((right.timestamp_us - left.timestamp_us) * numerator) / denominator,
  };
}

CanonicalSample Smooth(const CanonicalSample& input,
                       const std::optional<CanonicalSample>& prior) {
  CanonicalSample output = input;
  if (prior) {
    const auto smooth = [](float previous, float current) {
      return internal::CanonicalFloat(static_cast<double>(previous) * 0.75 +
                                      static_cast<double>(current) * 0.25);
    };
    output.position = {smooth(prior->position.x, input.position.x),
                       smooth(prior->position.y, input.position.y)};
    output.pressure = smooth(prior->pressure, input.pressure);
    output.tilt = {smooth(prior->tilt.x, input.tilt.x),
                   smooth(prior->tilt.y, input.tilt.y)};
  }
  output.pressure = MapPressure(output.pressure);
  return output;
}

PreviewPrimitive MakePrimitive(const BrushDescriptor& brush,
                               const VectorPoint& point) {
  return PreviewPrimitive{.position = point.position,
                          .radius = point.radius,
                          .rotation_degrees = 0.0F,
                          .opacity = brush.opacity};
}

PreviewPrimitive MakePrimitive(const Dab& dab) {
  return PreviewPrimitive{.position = dab.position,
                          .radius = dab.radius,
                          .rotation_degrees = dab.rotation_degrees,
                          .opacity = dab.opacity};
}

void AppendGeometry(StrokeId stroke_id, const BrushDescriptor& brush,
                    const CanonicalSample& sample, CandidateState* state) {
  if (brush.type == BrushType::kVector) {
    const VectorPoint point{
        .position = sample.position,
        .radius = internal::CanonicalFloat(
            static_cast<double>(brush.size) * (0.2 + 0.8 * sample.pressure) * 0.5),
    };
    state->stroke.vector_points.push_back(point);
    state->preview_confirmed.push_back(MakePrimitive(brush, point));
  } else {
    if (state->prior_dab_position) {
      const double dx = static_cast<double>(sample.position.x) -
                        state->prior_dab_position->x;
      const double dy = static_cast<double>(sample.position.y) -
                        state->prior_dab_position->y;
      const double minimum_distance =
          static_cast<double>(brush.size) * brush.spacing;
      if (dx * dx + dy * dy < minimum_distance * minimum_distance) {
        ++state->incremental_work_count;
        return;
      }
    }
    const uint64_t dab_index = state->stroke.dabs.size();
    internal::Pcg32 position_random =
        internal::MakeStrokeRandom(brush, stroke_id, 1, dab_index);
    internal::Pcg32 rotation_random =
        internal::MakeStrokeRandom(brush, stroke_id, 2, dab_index);
    const double jitter_extent = static_cast<double>(brush.size) * brush.jitter;
    const float jitter_x = internal::CanonicalFloat(
        static_cast<double>(position_random.SignedUnitFloat()) * jitter_extent);
    const float jitter_y = internal::CanonicalFloat(
        static_cast<double>(position_random.SignedUnitFloat()) * jitter_extent);
    const Dab dab{
        .position = {
            internal::CanonicalFloat(static_cast<double>(sample.position.x) + jitter_x),
            internal::CanonicalFloat(static_cast<double>(sample.position.y) + jitter_y),
        },
        .radius = internal::CanonicalFloat(
            static_cast<double>(brush.size) * (0.25 + 0.75 * sample.pressure) * 0.5),
        .rotation_degrees = internal::CanonicalFloat(
            static_cast<double>(rotation_random.UnitFloat()) * 360.0),
        .opacity = brush.opacity,
    };
    state->stroke.dabs.push_back(dab);
    state->prior_dab_position = sample.position;
    state->preview_confirmed.push_back(MakePrimitive(dab));
  }
  ++state->incremental_work_count;
}

void ResampleAndAppend(StrokeId stroke_id, const BrushDescriptor& brush,
                       const CanonicalSample& input, CandidateState* state) {
  if (!state->prior_resampled) {
    CanonicalSample smoothed = Smooth(input, state->prior_smoothed);
    AppendGeometry(stroke_id, brush, smoothed, state);
    state->prior_resampled = input;
    state->prior_smoothed = smoothed;
    return;
  }
  const uint64_t delta = input.timestamp_us - state->prior_resampled->timestamp_us;
  const uint64_t segments = std::max<uint64_t>(1, (delta + kResampleIntervalUs - 1) /
                                                     kResampleIntervalUs);
  const CanonicalSample left = *state->prior_resampled;
  for (uint64_t segment = 1; segment <= segments; ++segment) {
    const CanonicalSample resampled = Interpolate(left, input, segment, segments);
    CanonicalSample smoothed = Smooth(resampled, state->prior_smoothed);
    AppendGeometry(stroke_id, brush, smoothed, state);
    state->prior_smoothed = smoothed;
  }
  state->prior_resampled = input;
}

std::vector<PreviewPrimitive> Predict(const CandidateState& state) {
  if (state.preview_confirmed.size() < 2) return {};
  const PreviewPrimitive& prior =
      state.preview_confirmed[state.preview_confirmed.size() - 2];
  const PreviewPrimitive& last = state.preview_confirmed.back();
  PreviewPrimitive predicted = last;
  predicted.position.x = internal::CanonicalFloat(
      static_cast<double>(last.position.x) +
      (static_cast<double>(last.position.x) - prior.position.x) * 0.5);
  predicted.position.y = internal::CanonicalFloat(
      static_cast<double>(last.position.y) +
      (static_cast<double>(last.position.y) - prior.position.y) * 0.5);
  return {predicted};
}

Status ValidateBatch(const PointerSampleBatch& batch, PointerId pointer_id,
                     const CandidateState& state,
                     std::vector<CanonicalSample>* transformed) {
  if (transformed == nullptr || batch.view_id == 0 ||
      batch.viewport_revision == 0 || batch.samples.empty() ||
      !internal::IsValidTransform(batch.view_to_world) ||
      batch.device.platform_classified_palm || batch.device.eraser_tip) {
    return Status::kInvalidArgument;
  }
  transformed->clear();
  transformed->reserve(batch.samples.size());
  uint64_t expected_sequence = state.has_input ? state.next_sample_sequence
                                                : batch.samples.front().sample_sequence;
  uint64_t prior_timestamp = state.has_input ? state.prior_input_timestamp : 0;
  bool first = !state.has_input;
  for (const PointerSample& sample : batch.samples) {
    if (sample.pointer_id != pointer_id || sample.sample_sequence != expected_sequence ||
        (!first && sample.timestamp_us < prior_timestamp) ||
        !internal::IsFinite(sample.pressure) ||
        !internal::IsFinite(sample.tilt.x) || !internal::IsFinite(sample.tilt.y) ||
        !internal::IsFinite(sample.contact_size.x) ||
        !internal::IsFinite(sample.contact_size.y)) {
      return Status::kSequenceError;
    }
    if (sample.phase == PointerPhase::kHover) return Status::kInvalidArgument;
    Vec2 world;
    Status status = internal::TransformPoint(batch.view_to_world, sample.position, &world);
    if (status != Status::kOk) return status;
    const bool has_pressure =
        (batch.device.capabilities & kCapabilityPressure) != 0U;
    const bool has_tilt = (batch.device.capabilities & kCapabilityTilt) != 0U;
    const float pressure = has_pressure ? sample.pressure : 0.5F;
    if (pressure < 0.0F || pressure > 1.0F) return Status::kInvalidArgument;
    transformed->push_back(CanonicalSample{
        .position = world,
        .pressure = internal::CanonicalFloat(pressure),
        .tilt = has_tilt ? Vec2{internal::CanonicalFloat(sample.tilt.x),
                               internal::CanonicalFloat(sample.tilt.y)}
                         : Vec2{},
        .timestamp_us = sample.timestamp_us,
    });
    ++expected_sequence;
    prior_timestamp = sample.timestamp_us;
    first = false;
  }
  return Status::kOk;
}

}  // namespace

class StrokeSession::Impl {
 public:
  Impl(StrokeId stroke_id, PointerId pointer_id, BrushDescriptor descriptor,
       PreviewSink& sink)
      : id(stroke_id), pointer(pointer_id), brush(std::move(descriptor)), preview(sink) {
    state.stroke.id = id;
    state.stroke.brush = brush;
  }

  Status Process(const PointerSampleBatch& batch, bool beginning) {
    if ((beginning && active) || (!beginning && !active)) return Status::kInvalidState;
    if (id == 0 || pointer == 0) {
      return Status::kInvalidArgument;
    }
    const Status brush_status = internal::ValidateBrush(brush);
    if (brush_status != Status::kOk) return brush_status;
    std::vector<CanonicalSample> transformed;
    Status status = ValidateBatch(batch, pointer, state, &transformed);
    if (status != Status::kOk) return status;

    const size_t old_sample_count = state.stroke.confirmed_samples.size();
    const size_t old_vector_count = state.stroke.vector_points.size();
    const size_t old_dab_count = state.stroke.dabs.size();
    const size_t old_preview_count = state.preview_confirmed.size();
    const auto old_resampled = state.prior_resampled;
    const auto old_smoothed = state.prior_smoothed;
    const auto old_dab_position = state.prior_dab_position;
    const uint64_t old_input_timestamp = state.prior_input_timestamp;
    const uint64_t old_next_sequence = state.next_sample_sequence;
    const bool old_has_input = state.has_input;
    const size_t old_work_count = state.incremental_work_count;
    const auto rollback = [this, old_sample_count, old_vector_count, old_dab_count,
                           old_preview_count, old_resampled, old_smoothed,
                           old_dab_position,
                           old_input_timestamp, old_next_sequence, old_has_input,
                           old_work_count]() {
      state.stroke.confirmed_samples.resize(old_sample_count);
      state.stroke.vector_points.resize(old_vector_count);
      state.stroke.dabs.resize(old_dab_count);
      state.preview_confirmed.resize(old_preview_count);
      state.prior_resampled = old_resampled;
      state.prior_smoothed = old_smoothed;
      state.prior_dab_position = old_dab_position;
      state.prior_input_timestamp = old_input_timestamp;
      state.next_sample_sequence = old_next_sequence;
      state.has_input = old_has_input;
      state.incremental_work_count = old_work_count;
    };
    try {
      state.stroke.confirmed_samples.reserve(old_sample_count + transformed.size());
      state.preview_confirmed.reserve(old_preview_count + transformed.size());
      for (const CanonicalSample& sample : transformed) {
        state.stroke.confirmed_samples.push_back(sample);
        ResampleAndAppend(id, brush, sample, &state);
      }
      state.has_input = true;
      state.prior_input_timestamp = transformed.back().timestamp_us;
      state.next_sample_sequence = batch.samples.back().sample_sequence + 1;
    } catch (const std::exception&) {
      rollback();
      return Status::kInvalidArgument;
    }

    if (beginning) {
      status = preview.Begin(id, brush);
      if (status != Status::kOk) {
        rollback();
        return status;
      }
    }
    PreviewStrokeUpdate update{
        .stroke_id = id,
        .revision = preview_revision + 1,
        .view_id = batch.view_id,
        .viewport_revision = batch.viewport_revision,
        .brush = brush,
        .truncate_confirmed_to = old_preview_count,
        .confirmed_append = std::vector<PreviewPrimitive>(
            state.preview_confirmed.begin() +
                static_cast<std::ptrdiff_t>(old_preview_count),
            state.preview_confirmed.end()),
        .predicted_tail = Predict(state),
    };
    status = preview.Push(update);
    if (status != Status::kOk) {
      if (beginning) preview.Cancel(id);
      rollback();
      return status;
    }
    ++preview_revision;
    active = true;
    return Status::kOk;
  }

  StrokeId id;
  PointerId pointer;
  BrushDescriptor brush;
  PreviewSink& preview;
  CandidateState state;
  uint64_t preview_revision = 0;
  bool active = false;
};

StrokeSession::StrokeSession(StrokeId stroke_id, PointerId pointer_id,
                             BrushDescriptor brush, PreviewSink& preview_sink)
    : impl_(std::make_unique<Impl>(stroke_id, pointer_id, std::move(brush),
                                  preview_sink)) {}

StrokeSession::~StrokeSession() = default;

Status StrokeSession::Begin(const PointerSampleBatch& batch) {
  return impl_->Process(batch, true);
}

Status StrokeSession::Push(const PointerSampleBatch& batch) {
  return impl_->Process(batch, false);
}

Status StrokeSession::End(Stroke* stroke) {
  if (!impl_->active || stroke == nullptr ||
      internal::ValidateStroke(impl_->state.stroke) != Status::kOk) {
    return Status::kInvalidState;
  }
  *stroke = std::move(impl_->state.stroke);
  impl_->active = false;
  return Status::kOk;
}

Status StrokeSession::Cancel() {
  if (!impl_->active) return Status::kInvalidState;
  const Status status = impl_->preview.Cancel(impl_->id);
  impl_->active = false;
  impl_->state = CandidateState{};
  return status;
}

StrokeId StrokeSession::stroke_id() const { return impl_->id; }
size_t StrokeSession::confirmed_input_count() const {
  return impl_->state.stroke.confirmed_samples.size();
}
size_t StrokeSession::incremental_work_count() const {
  return impl_->state.incremental_work_count;
}
uint64_t StrokeSession::preview_revision() const { return impl_->preview_revision; }
bool StrokeSession::active() const { return impl_->active; }

}  // namespace canvas::poc02
