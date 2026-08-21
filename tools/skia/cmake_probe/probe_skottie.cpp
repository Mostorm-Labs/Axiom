#include "modules/skottie/include/Skottie.h"

int main() {
  constexpr char animation[] =
      R"({"v":"5.7.4","fr":60,"ip":0,"op":1,"w":8,"h":8,"layers":[]})";
  return skottie::Animation::Make(animation, sizeof(animation) - 1) ? 0 : 1;
}
