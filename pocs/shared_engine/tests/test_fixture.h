#ifndef CANVAS_POC_TEST_FIXTURE_H_
#define CANVAS_POC_TEST_FIXTURE_H_

#include <memory>
#include <string>

#include "document.h"

namespace canvas::poc01::test {

std::shared_ptr<AssetRegistry> MakeAssets();
std::unique_ptr<Document> MakeDocument();
std::string FixedReplay();

}  // namespace canvas::poc01::test

#endif  // CANVAS_POC_TEST_FIXTURE_H_
