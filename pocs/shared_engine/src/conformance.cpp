#include "conformance.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>

#include "document.h"
#include "foundation.h"
#include "operations.h"

namespace canvas::poc01 {
namespace {

constexpr std::string_view kReplay =
    "{\"v\":1,\"seq\":1,\"op\":\"create\",\"node\":{\"id\":20,\"type\":\"rect\",\"order\":20,\"x\":10,\"y\":20,\"width\":30,\"height\":40,\"color\":[1,2,3,255]}}\n"
    "{\"v\":1,\"seq\":2,\"op\":\"move\",\"id\":20,\"dx\":-10,\"dy\":5}\n"
    "{\"v\":1,\"seq\":3,\"op\":\"create\",\"node\":{\"id\":30,\"type\":\"rect\",\"order\":10,\"x\":1,\"y\":2,\"width\":3,\"height\":4,\"color\":[4,5,6,255]}}\n"
    "{\"v\":1,\"seq\":4,\"op\":\"delete\",\"id\":30}\n";

std::unique_ptr<Document> MakeDocument() {
  return std::make_unique<Document>(std::make_shared<AssetRegistry>(), 800, 600,
                                    Color{244, 245, 247, 255});
}

std::string CreateRect(std::string_view x) {
  return "{\"v\":1,\"seq\":1,\"op\":\"create\",\"node\":{\"id\":1,\"type\":\"rect\",\"order\":1,\"x\":" +
         std::string(x) +
         ",\"y\":0,\"width\":1,\"height\":1,\"color\":[0,0,0,255]}}\n";
}

const RectNode& OnlyRect(const Document& document) {
  return std::get<RectNode>(document.state().nodes.begin()->second);
}

void RecordFailure(std::string* failure, std::string_view message) {
  if (failure->empty()) *failure = message;
}

void EncodeCase(CanonicalEncoder& encoder, std::string_view name,
                canvas_poc_status_t status, uint32_t bits,
                std::string_view digest) {
  encoder.String(name);
  encoder.U32(static_cast<uint32_t>(status));
  encoder.U32(bits);
  encoder.String(digest);
}

void CheckAcceptedFloat(CanonicalEncoder& encoder, std::string_view name,
                        std::string_view token, uint32_t expected_bits,
                        std::string* failure) {
  auto document = MakeDocument();
  const canvas_poc_status_t status = ApplyOperations(*document, CreateRect(token));
  uint32_t bits = 0;
  if (status == CANVAS_POC_STATUS_OK && document->state().nodes.size() == 1 &&
      std::holds_alternative<RectNode>(document->state().nodes.begin()->second)) {
    bits = std::bit_cast<uint32_t>(OnlyRect(*document).x);
  }
  if (status != CANVAS_POC_STATUS_OK || bits != expected_bits) {
    RecordFailure(failure, std::string(name) + ":status=" +
                               std::to_string(static_cast<uint32_t>(status)) +
                               ",bits=" + std::to_string(bits));
  }
  EncodeCase(encoder, name, status, bits, document->Digest());
}

void CheckRejectedAtomic(CanonicalEncoder& encoder, std::string_view name,
                         std::string_view operations,
                         canvas_poc_status_t expected_status,
                         std::string* failure) {
  auto document = MakeDocument();
  const std::string before = document->Digest();
  const canvas_poc_status_t status = ApplyOperations(*document, operations);
  const bool atomic = document->Digest() == before &&
                      document->state().revision == 0 &&
                      document->state().last_sequence == 0 &&
                      document->state().nodes.empty();
  if (status != expected_status || !atomic) {
    RecordFailure(failure, std::string(name) + ":status=" +
                               std::to_string(static_cast<uint32_t>(status)) +
                               ",atomic=" + (atomic ? "true" : "false"));
  }
  EncodeCase(encoder, name, status, atomic ? 0U : 1U, document->Digest());
}

}  // namespace

CoreConformanceResult RunCoreConformance() {
  CanonicalEncoder corpus;
  corpus.String("canvas-poc01-core-conformance-v1");
  uint32_t case_count = 0;
  std::string failure;

  CanonicalEncoder positive_zero;
  CanonicalEncoder negative_zero;
  positive_zero.F32(0.0F);
  negative_zero.F32(-0.0F);
  if (!std::equal(positive_zero.data().begin(), positive_zero.data().end(),
                  negative_zero.data().begin(), negative_zero.data().end())) {
    RecordFailure(&failure, "canonical-encoder-signed-zero");
  }
  CheckAcceptedFloat(corpus, "canonical-zero-positive", "0", 0x00000000U,
                     &failure);
  ++case_count;
  CheckAcceptedFloat(corpus, "canonical-zero-negative", "-0.0", 0x00000000U,
                     &failure);
  ++case_count;
  CheckAcceptedFloat(corpus, "minimum-subnormal",
                     "1.401298464324817070923729583289916131280e-45",
                     0x00000001U, &failure);
  ++case_count;
  CheckAcceptedFloat(corpus, "ties-to-even-midpoint",
                     "1.000000059604644775390625", 0x3f800000U, &failure);
  ++case_count;
  CheckAcceptedFloat(corpus, "maximum-finite",
                     "3.40282346638528859811704183484516925440e38",
                     0x7f7fffffU, &failure);
  ++case_count;

  CheckRejectedAtomic(corpus, "float32-input-overflow", CreateRect("3.4028236e38"),
                      CANVAS_POC_STATUS_PARSE_ERROR, &failure);
  ++case_count;
  CheckRejectedAtomic(corpus, "nan-token", CreateRect("NaN"),
                      CANVAS_POC_STATUS_PARSE_ERROR, &failure);
  ++case_count;
  CheckRejectedAtomic(corpus, "infinity-token", CreateRect("Infinity"),
                      CANVAS_POC_STATUS_PARSE_ERROR, &failure);
  ++case_count;

  const std::string move_overflow =
      CreateRect("0") +
      "{\"v\":1,\"seq\":2,\"op\":\"move\",\"id\":1,\"dx\":3.40282346638528859811704183484516925440e38,\"dy\":0}\n"
      "{\"v\":1,\"seq\":3,\"op\":\"move\",\"id\":1,\"dx\":3.40282346638528859811704183484516925440e38,\"dy\":0}\n";
  CheckRejectedAtomic(corpus, "move-result-overflow", move_overflow,
                      CANVAS_POC_STATUS_PARSE_ERROR, &failure);
  ++case_count;

  auto first = MakeDocument();
  auto second = MakeDocument();
  const canvas_poc_status_t first_status = ApplyOperations(*first, kReplay);
  const canvas_poc_status_t second_status = ApplyOperations(*second, kReplay);
  const DocumentState& lhs = first->state();
  const DocumentState& rhs = second->state();
  const bool replay_equal =
      first_status == CANVAS_POC_STATUS_OK &&
      second_status == CANVAS_POC_STATUS_OK && lhs.revision == rhs.revision &&
      lhs.last_sequence == rhs.last_sequence &&
      lhs.nodes.size() == rhs.nodes.size() && first->Digest() == second->Digest() &&
      lhs.nodes.size() == 1 && lhs.nodes.begin()->first == 20 &&
      rhs.nodes.begin()->first == 20 &&
      Header(lhs.nodes.begin()->second).order == 20 &&
      Header(rhs.nodes.begin()->second).order == 20 &&
      std::bit_cast<uint32_t>(Header(lhs.nodes.begin()->second).translation_x) ==
          std::bit_cast<uint32_t>(Header(rhs.nodes.begin()->second).translation_x) &&
      std::bit_cast<uint32_t>(Header(lhs.nodes.begin()->second).translation_y) ==
          std::bit_cast<uint32_t>(Header(rhs.nodes.begin()->second).translation_y);
  if (!replay_equal) RecordFailure(&failure, "fresh-document-replay");
  EncodeCase(corpus, "fresh-document-replay", first_status,
             static_cast<uint32_t>(lhs.nodes.size()), first->Digest());
  ++case_count;

  CoreConformanceResult result;
  result.corpus_digest = HashHex(HashBytes(corpus.data()));
  result.replay_digest = first->Digest();
  result.case_count = case_count;
  result.replay_revision = lhs.revision;
  result.replay_sequence = lhs.last_sequence;
  result.passed = failure.empty();
  result.failure = std::move(failure);
  return result;
}

std::string CoreConformanceJsonFields(const CoreConformanceResult& result) {
  return "\"core_conformance\":{\"version\":1,\"case_count\":" +
         std::to_string(result.case_count) + ",\"corpus_digest\":\"" +
         result.corpus_digest + "\",\"replay_digest\":\"" +
         result.replay_digest + "\",\"replay_revision\":" +
         std::to_string(result.replay_revision) + ",\"replay_sequence\":" +
         std::to_string(result.replay_sequence) + ",\"passed\":" +
         (result.passed ? "true" : "false") + ",\"failure\":\"" +
         result.failure + "\"}";
}

}  // namespace canvas::poc01
