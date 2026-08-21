#include "include/core/SkStream.h"
#include "include/docs/SkPDFDocument.h"

int main() {
  SkDynamicMemoryWStream stream;
  auto document = SkPDF::MakeDocument(&stream);
  return document ? 0 : 1;
}
