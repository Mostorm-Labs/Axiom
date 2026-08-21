#include "arc/protocol.h"

#include <stddef.h>

_Static_assert(sizeof(((arc_preview_begin_v0*)0)->stroke_id) == 8,
               "stroke ids must remain fixed-width");
_Static_assert(sizeof(((arc_preview_update_v0*)0)->preview_revision) == 8,
               "revisions must remain fixed-width");
_Static_assert(offsetof(arc_preview_begin_v0, struct_size) == 0,
               "struct_size must be the first ABI field");
_Static_assert(offsetof(arc_preview_begin_v0, abi_version) == 4,
               "abi_version must follow struct_size");

int main(void) {
  arc_preview_begin_v0 begin = {0};
  begin.struct_size = sizeof(begin);
  begin.abi_version = ARC_ABI_VERSION;
  begin.schema_version = ARC_PROTOCOL_SCHEMA_VERSION;
  return begin.struct_size == 0;
}
