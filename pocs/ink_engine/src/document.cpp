#include "canvas_poc02/ink_engine.h"

#include <algorithm>
#include <exception>
#include <limits>

#include <nlohmann/json.hpp>

#include "foundation.h"

namespace canvas::poc02 {
namespace {

using Json = nlohmann::json;

Json EncodeVec(Vec2 value) { return Json::array({value.x, value.y}); }

Json EncodeBrushJson(const BrushDescriptor& brush) {
  return Json{{"type", brush.type == BrushType::kVector ? "vector" : "dab"},
              {"brush_version", brush.brush_version},
              {"algorithm_version", brush.algorithm_version},
              {"size", brush.size},
              {"spacing", brush.spacing},
              {"opacity", brush.opacity},
              {"jitter", brush.jitter},
              {"resource_id", brush.resource_id},
              {"resource_content_hash", brush.resource_content_hash}};
}

Json EncodeStrokeJson(const Stroke& stroke) {
  Json samples = Json::array();
  for (const auto& sample : stroke.confirmed_samples) {
    samples.push_back(Json{{"position", EncodeVec(sample.position)},
                           {"pressure", sample.pressure},
                           {"tilt", EncodeVec(sample.tilt)},
                           {"timestamp_us", sample.timestamp_us}});
  }
  Json vector_points = Json::array();
  for (const auto& point : stroke.vector_points) {
    vector_points.push_back(Json{{"position", EncodeVec(point.position)},
                                 {"radius", point.radius}});
  }
  Json dabs = Json::array();
  for (const auto& dab : stroke.dabs) {
    dabs.push_back(Json{{"position", EncodeVec(dab.position)},
                        {"radius", dab.radius},
                        {"rotation_degrees", dab.rotation_degrees},
                        {"opacity", dab.opacity}});
  }
  return Json{{"id", stroke.id},
              {"brush", EncodeBrushJson(stroke.brush)},
              {"confirmed_samples", std::move(samples)},
              {"vector_points", std::move(vector_points)},
              {"dabs", std::move(dabs)}};
}

bool ExactKeys(const Json& value, std::initializer_list<std::string_view> keys) {
  if (!value.is_object() || value.size() != keys.size()) return false;
  return std::all_of(keys.begin(), keys.end(), [&value](std::string_view key) {
    return value.contains(std::string(key));
  });
}

Status ParseFloat(const Json& value, float* output) {
  if (output == nullptr || !value.is_number()) return Status::kParseError;
  try {
    *output = internal::CanonicalFloat(value.get<double>());
  } catch (const std::exception&) {
    return Status::kInvalidArgument;
  }
  return Status::kOk;
}

Status ParseVec(const Json& value, Vec2* output) {
  if (output == nullptr || !value.is_array() || value.size() != 2) {
    return Status::kParseError;
  }
  Status status = ParseFloat(value[0], &output->x);
  return status == Status::kOk ? ParseFloat(value[1], &output->y) : status;
}

Status ParseBrushJson(const Json& value, BrushDescriptor* brush) {
  if (brush == nullptr || !ExactKeys(value, {"type", "brush_version",
      "algorithm_version", "size", "spacing", "opacity", "jitter",
      "resource_id", "resource_content_hash"})) {
    return Status::kParseError;
  }
  if (!value["type"].is_string()) return Status::kParseError;
  const std::string type = value["type"].get<std::string>();
  if (type == "vector") brush->type = BrushType::kVector;
  else if (type == "dab") brush->type = BrushType::kDab;
  else return Status::kParseError;
  try {
    brush->brush_version = value["brush_version"].get<uint32_t>();
    brush->algorithm_version = value["algorithm_version"].get<uint32_t>();
    brush->resource_id = value["resource_id"].get<std::string>();
    brush->resource_content_hash = value["resource_content_hash"].get<std::string>();
  } catch (const std::exception&) {
    return Status::kParseError;
  }
  Status status = ParseFloat(value["size"], &brush->size);
  if (status == Status::kOk) status = ParseFloat(value["spacing"], &brush->spacing);
  if (status == Status::kOk) status = ParseFloat(value["opacity"], &brush->opacity);
  if (status == Status::kOk) status = ParseFloat(value["jitter"], &brush->jitter);
  return status == Status::kOk ? internal::ValidateBrush(*brush) : status;
}

Status ParseStrokeJson(const Json& value, Stroke* stroke) {
  if (stroke == nullptr || !ExactKeys(value, {"id", "brush", "confirmed_samples",
      "vector_points", "dabs"})) {
    return Status::kParseError;
  }
  try {
    stroke->id = value["id"].get<StrokeId>();
  } catch (const std::exception&) {
    return Status::kParseError;
  }
  Status status = ParseBrushJson(value["brush"], &stroke->brush);
  if (status != Status::kOk) return status;
  const Json& samples = value["confirmed_samples"];
  const Json& vector_points = value["vector_points"];
  const Json& dabs = value["dabs"];
  if (!samples.is_array() || !vector_points.is_array() || !dabs.is_array() ||
      samples.size() > 1000000 || vector_points.size() > 1000000 ||
      dabs.size() > 1000000) {
    return Status::kParseError;
  }
  for (const Json& item : samples) {
    if (!ExactKeys(item, {"position", "pressure", "tilt", "timestamp_us"})) {
      return Status::kParseError;
    }
    CanonicalSample sample;
    status = ParseVec(item["position"], &sample.position);
    if (status == Status::kOk) status = ParseFloat(item["pressure"], &sample.pressure);
    if (status == Status::kOk) status = ParseVec(item["tilt"], &sample.tilt);
    if (status != Status::kOk) return status;
    try {
      sample.timestamp_us = item["timestamp_us"].get<uint64_t>();
    } catch (const std::exception&) {
      return Status::kParseError;
    }
    stroke->confirmed_samples.push_back(sample);
  }
  for (const Json& item : vector_points) {
    if (!ExactKeys(item, {"position", "radius"})) return Status::kParseError;
    VectorPoint point;
    status = ParseVec(item["position"], &point.position);
    if (status == Status::kOk) status = ParseFloat(item["radius"], &point.radius);
    if (status != Status::kOk) return status;
    stroke->vector_points.push_back(point);
  }
  for (const Json& item : dabs) {
    if (!ExactKeys(item, {"position", "radius", "rotation_degrees", "opacity"})) {
      return Status::kParseError;
    }
    Dab dab;
    status = ParseVec(item["position"], &dab.position);
    if (status == Status::kOk) status = ParseFloat(item["radius"], &dab.radius);
    if (status == Status::kOk) {
      status = ParseFloat(item["rotation_degrees"], &dab.rotation_degrees);
    }
    if (status == Status::kOk) status = ParseFloat(item["opacity"], &dab.opacity);
    if (status != Status::kOk) return status;
    stroke->dabs.push_back(dab);
  }
  return internal::ValidateStroke(*stroke);
}

}  // namespace

Status StrokeDocument::Apply(const AddStrokeOperation& operation) {
  if (operation.schema_version != AddStrokeOperation::kSchemaVersion) {
    return Status::kUnsupportedVersion;
  }
  if (operation.sequence != operation_sequence_ + 1) {
    return Status::kSequenceError;
  }
  if (internal::ValidateStroke(operation.stroke) != Status::kOk) {
    return Status::kInvalidArgument;
  }
  if (Find(operation.stroke.id) != nullptr) return Status::kInvalidArgument;
  strokes_.push_back(operation.stroke);
  operation_sequence_ = operation.sequence;
  ++revision_;
  return Status::kOk;
}

const Stroke* StrokeDocument::Find(StrokeId id) const {
  const auto iterator = std::find_if(strokes_.begin(), strokes_.end(),
                                     [id](const Stroke& stroke) { return stroke.id == id; });
  return iterator == strokes_.end() ? nullptr : &*iterator;
}

std::string StrokeDocument::Digest() const {
  internal::CanonicalEncoder encoder;
  encoder.String("canvas-poc02-document-v1");
  encoder.U64(revision_);
  encoder.U64(operation_sequence_);
  encoder.U64(strokes_.size());
  for (const auto& stroke : strokes_) internal::EncodeStroke(stroke, &encoder);
  return internal::HashHex(encoder.bytes());
}

std::string SerializeAddStrokeNdjson(const AddStrokeOperation& operation) {
  const Json json{{"v", operation.schema_version},
                  {"seq", operation.sequence},
                  {"type", "add_stroke"},
                  {"stroke", EncodeStrokeJson(operation.stroke)}};
  return json.dump() + "\n";
}

Status ParseAddStrokeNdjson(std::string_view line, AddStrokeOperation* operation,
                            std::string* error) {
  if (operation == nullptr) return Status::kInvalidArgument;
  try {
    const Json json = Json::parse(line);
    if (!ExactKeys(json, {"v", "seq", "type", "stroke"}) ||
        !json["type"].is_string() || json["type"].get<std::string>() != "add_stroke") {
      if (error) *error = "operation must contain only v, seq, type=add_stroke, stroke";
      return Status::kParseError;
    }
    AddStrokeOperation candidate;
    candidate.schema_version = json["v"].get<uint32_t>();
    candidate.sequence = json["seq"].get<uint64_t>();
    Status status = ParseStrokeJson(json["stroke"], &candidate.stroke);
    if (status != Status::kOk) {
      if (error) *error = std::string(StatusName(status));
      return status;
    }
    if (candidate.schema_version != AddStrokeOperation::kSchemaVersion) {
      if (error) *error = "unsupported add-stroke schema version";
      return Status::kUnsupportedVersion;
    }
    *operation = std::move(candidate);
    if (error) error->clear();
    return Status::kOk;
  } catch (const std::exception& exception) {
    if (error) *error = exception.what();
    return Status::kParseError;
  }
}

}  // namespace canvas::poc02
