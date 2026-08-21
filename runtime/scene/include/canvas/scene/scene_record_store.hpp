#pragma once

#include "canvas/foundation/result.hpp"
#include "canvas/scene/scene_types.hpp"

#include <cstddef>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace canvas {

class SceneRecordStore final {
  public:
    class PreparedUpdate final {
      public:
        PreparedUpdate(PreparedUpdate&&) noexcept = default;
        PreparedUpdate& operator=(PreparedUpdate&&) noexcept = default;

        PreparedUpdate(const PreparedUpdate&) = delete;
        PreparedUpdate& operator=(const PreparedUpdate&) = delete;

        [[nodiscard]] std::span<const SceneRecord> records() const {
            return _records;
        }
        [[nodiscard]] const SceneRecord* find(ObjectId objectId) const;
        [[nodiscard]] WorldRect contentBounds() const;

      private:
        friend class SceneRecordStore;

        PreparedUpdate(std::vector<SceneRecord> records,
                       std::unordered_map<ObjectId, std::size_t, foundation::ObjectIdHash> index)
            : _records(std::move(records)), _index(std::move(index)) {}

        std::vector<SceneRecord> _records;
        std::unordered_map<ObjectId, std::size_t, foundation::ObjectIdHash> _index;
    };

    [[nodiscard]] std::span<const SceneRecord> records() const {
        return _records;
    }
    [[nodiscard]] const SceneRecord* find(ObjectId objectId) const;
    [[nodiscard]] WorldRect contentBounds() const;
    [[nodiscard]] std::size_t estimatedBytes() const;

    foundation::Result<PreparedUpdate> prepareReplace(std::span<const SceneRecord> records) const;
    foundation::Result<PreparedUpdate> prepareApply(std::span<const SceneMutation> mutations) const;
    void commit(PreparedUpdate update) noexcept;

  private:
    static foundation::Result<PreparedUpdate> buildPreparedUpdate(std::vector<SceneRecord> records);

    std::vector<SceneRecord> _records;
    std::unordered_map<ObjectId, std::size_t, foundation::ObjectIdHash> _index;
};

} // namespace canvas
