#include "canvas_poc02/ink_engine.h"

#include <exception>
#include <sstream>

#include <nlohmann/json.hpp>

#include "foundation.h"

namespace canvas::poc02 {
namespace {

using Json = nlohmann::json;

Status Number(const Json& value, float* output) {
  if (output == nullptr || !value.is_number()) return Status::kParseError;
  try {
    *output = internal::CanonicalFloat(value.get<double>());
  } catch (const std::exception&) {
    return Status::kInvalidArgument;
  }
  return Status::kOk;
}

Status Vec(const Json& value, Vec2* output) {
  if (output == nullptr || !value.is_array() || value.size() != 2) {
    return Status::kParseError;
  }
  Status status = Number(value[0], &output->x);
  return status == Status::kOk ? Number(value[1], &output->y) : status;
}

bool HasOnly(const Json& value, std::initializer_list<std::string_view> keys) {
  if (!value.is_object() || value.size() != keys.size()) return false;
  for (std::string_view key : keys) {
    if (!value.contains(std::string(key))) return false;
  }
  return true;
}

Status ParseHeader(const Json& json, ReplayFixture* fixture) {
  if (!HasOnly(json, {"v", "type", "stroke_id", "pointer_id", "operation_sequence",
                      "brush"}) ||
      json["type"] != "ink_fixture") {
    return Status::kParseError;
  }
  try {
    if (json["v"].get<uint32_t>() != 1) return Status::kUnsupportedVersion;
    fixture->stroke_id = json["stroke_id"].get<uint64_t>();
    fixture->pointer_id = json["pointer_id"].get<uint64_t>();
    fixture->operation_sequence = json["operation_sequence"].get<uint64_t>();
    const Json& brush = json["brush"];
    if (!HasOnly(brush, {"type", "brush_version", "algorithm_version", "size",
                         "spacing", "opacity", "jitter", "resource_id",
                         "resource_content_hash"})) {
      return Status::kParseError;
    }
    const std::string type = brush["type"].get<std::string>();
    if (type == "vector") fixture->brush.type = BrushType::kVector;
    else if (type == "dab") fixture->brush.type = BrushType::kDab;
    else return Status::kParseError;
    fixture->brush.brush_version = brush["brush_version"].get<uint32_t>();
    fixture->brush.algorithm_version = brush["algorithm_version"].get<uint32_t>();
    fixture->brush.resource_id = brush["resource_id"].get<std::string>();
    fixture->brush.resource_content_hash =
        brush["resource_content_hash"].get<std::string>();
    Status status = Number(brush["size"], &fixture->brush.size);
    if (status == Status::kOk) status = Number(brush["spacing"], &fixture->brush.spacing);
    if (status == Status::kOk) status = Number(brush["opacity"], &fixture->brush.opacity);
    if (status == Status::kOk) status = Number(brush["jitter"], &fixture->brush.jitter);
    return status == Status::kOk ? internal::ValidateBrush(fixture->brush) : status;
  } catch (const std::exception&) {
    return Status::kParseError;
  }
}

Status ParseBatch(const Json& json, PointerSampleBatch* batch) {
  if (!HasOnly(json, {"v", "type", "view_id", "viewport_revision",
                      "view_to_world", "device", "samples"}) ||
      json["type"] != "batch") {
    return Status::kParseError;
  }
  try {
    if (json["v"].get<uint32_t>() != 1) return Status::kUnsupportedVersion;
    batch->view_id = json["view_id"].get<uint64_t>();
    batch->viewport_revision = json["viewport_revision"].get<uint64_t>();
    const Json& transform = json["view_to_world"];
    if (!transform.is_array() || transform.size() != 6) return Status::kParseError;
    float* values[] = {&batch->view_to_world.m00, &batch->view_to_world.m01,
                       &batch->view_to_world.m10, &batch->view_to_world.m11,
                       &batch->view_to_world.tx, &batch->view_to_world.ty};
    for (size_t index = 0; index < 6; ++index) {
      const Status status = Number(transform[index], values[index]);
      if (status != Status::kOk) return status;
    }
    const Json& device = json["device"];
    if (!HasOnly(device, {"id", "tool", "capabilities", "barrel_button",
                          "eraser_tip", "platform_classified_palm"})) {
      return Status::kParseError;
    }
    batch->device.device_id = device["id"].get<uint64_t>();
    const std::string tool = device["tool"].get<std::string>();
    if (tool == "mouse") batch->device.tool = PointerTool::kMouse;
    else if (tool == "pen") batch->device.tool = PointerTool::kPen;
    else if (tool == "touch") batch->device.tool = PointerTool::kTouch;
    else return Status::kParseError;
    batch->device.capabilities = device["capabilities"].get<uint32_t>();
    batch->device.barrel_button = device["barrel_button"].get<bool>();
    batch->device.eraser_tip = device["eraser_tip"].get<bool>();
    batch->device.platform_classified_palm =
        device["platform_classified_palm"].get<bool>();
    if (!json["samples"].is_array() || json["samples"].empty() ||
        json["samples"].size() > 4096) {
      return Status::kParseError;
    }
    for (const Json& item : json["samples"]) {
      if (!HasOnly(item, {"pointer_id", "sample_sequence", "position", "pressure",
                          "tilt", "contact_size", "timestamp_us", "phase"})) {
        return Status::kParseError;
      }
      PointerSample sample;
      sample.pointer_id = item["pointer_id"].get<uint64_t>();
      sample.sample_sequence = item["sample_sequence"].get<uint64_t>();
      sample.timestamp_us = item["timestamp_us"].get<uint64_t>();
      Status status = Vec(item["position"], &sample.position);
      if (status == Status::kOk) status = Number(item["pressure"], &sample.pressure);
      if (status == Status::kOk) status = Vec(item["tilt"], &sample.tilt);
      if (status == Status::kOk) status = Vec(item["contact_size"], &sample.contact_size);
      if (status != Status::kOk) return status;
      const std::string phase = item["phase"].get<std::string>();
      if (phase == "down") sample.phase = PointerPhase::kDown;
      else if (phase == "move") sample.phase = PointerPhase::kMove;
      else if (phase == "up") sample.phase = PointerPhase::kUp;
      else if (phase == "hover") sample.phase = PointerPhase::kHover;
      else return Status::kParseError;
      batch->samples.push_back(sample);
    }
    return Status::kOk;
  } catch (const std::exception&) {
    return Status::kParseError;
  }
}

}  // namespace

Status ParseReplayFixture(std::string_view ndjson, ReplayFixture* fixture,
                          std::string* error) {
  if (fixture == nullptr) return Status::kInvalidArgument;
  try {
    ReplayFixture candidate;
    std::istringstream stream{std::string(ndjson)};
    std::string line;
    bool header = false;
    while (std::getline(stream, line)) {
      if (line.empty()) continue;
      const Json json = Json::parse(line);
      Status status = Status::kOk;
      if (!header) {
        status = ParseHeader(json, &candidate);
        header = true;
      } else {
        PointerSampleBatch batch;
        status = ParseBatch(json, &batch);
        if (status == Status::kOk) candidate.batches.push_back(std::move(batch));
      }
      if (status != Status::kOk) {
        if (error) *error = std::string(StatusName(status));
        return status;
      }
    }
    if (!header || candidate.batches.empty()) {
      if (error) *error = "fixture requires one header and at least one batch";
      return Status::kParseError;
    }
    *fixture = std::move(candidate);
    if (error) error->clear();
    return Status::kOk;
  } catch (const std::exception& exception) {
    if (error) *error = exception.what();
    return Status::kParseError;
  }
}

Status RunReplayFixture(const ReplayFixture& fixture, StrokeDocument* document,
                        DefaultPreviewSink* sink,
                        AddStrokeOperation* committed_operation,
                        std::string* error) {
  if (document == nullptr || sink == nullptr || committed_operation == nullptr ||
      fixture.batches.empty()) {
    return Status::kInvalidArgument;
  }
  InputRouter router(*document, *sink);
  Status status = router.Begin(fixture.stroke_id, fixture.pointer_id, fixture.brush,
                               fixture.batches.front());
  for (size_t index = 1; status == Status::kOk && index < fixture.batches.size(); ++index) {
    const auto& batch = fixture.batches[index];
    const uint64_t now = batch.samples.back().timestamp_us;
    status = router.Submit(batch, now);
    if (status == Status::kOk) status = router.Drain(now);
  }
  if (status == Status::kOk) {
    status = router.End(fixture.operation_sequence, committed_operation);
  }
  if (status == Status::kOk) {
    status = router.AcknowledgeCanonicalVisible(committed_operation->stroke.id,
                                                document->revision());
  }
  if (status != Status::kOk && error) *error = std::string(StatusName(status));
  return status;
}

}  // namespace canvas::poc02
