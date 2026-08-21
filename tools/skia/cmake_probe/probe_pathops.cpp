#include "include/core/SkPath.h"
#include "include/pathops/SkPathOps.h"

int main() {
  SkPath left;
  SkPath right;
  left.addRect({0, 0, 8, 8});
  right.addRect({4, 4, 12, 12});
  return Op(left, right, kUnion_SkPathOp).has_value() ? 0 : 1;
}
