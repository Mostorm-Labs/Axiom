#include "include/core/SkPath.h"
#include "include/pathops/SkPathOps.h"

int main() {
  const SkPath left = SkPath::Rect({0, 0, 8, 8});
  const SkPath right = SkPath::Rect({4, 4, 12, 12});
  return Op(left, right, kUnion_SkPathOp).has_value() ? 0 : 1;
}
