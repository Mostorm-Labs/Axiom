#include "include/core/SkStream.h"
#include "modules/svg/include/SkSVGDOM.h"

int main() {
  constexpr char svg[] = "<svg xmlns='http://www.w3.org/2000/svg' width='8' height='8'/>";
  SkMemoryStream stream(svg, sizeof(svg) - 1);
  return SkSVGDOM::Builder().make(stream) ? 0 : 1;
}
