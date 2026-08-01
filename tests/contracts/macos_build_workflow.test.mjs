import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";

const repositoryRoot = resolve(dirname(fileURLToPath(import.meta.url)), "../..");
const workflow = readFileSync(
  resolve(repositoryRoot, ".github/workflows/macos-build.yml"),
  "utf8",
);

test("macOS workflow uses the arm64 runner and safe triggers", () => {
  assert.match(workflow, /^  pull_request:\s*$/m);
  assert.match(workflow, /^  workflow_dispatch:\s*$/m);
  assert.match(workflow, /^    runs-on: macos-14\s*$/m);
  assert.match(workflow, /architecture=.*uname -m/);
  assert.match(workflow, /requires an arm64 runner/);
  assert.match(workflow, /^permissions:\s*\n  contents: read\s*$/m);
  assert.doesNotMatch(workflow, /continue-on-error:/);
});

test("macOS workflow validates Node and vcpkg inputs before configure", () => {
  assert.match(workflow, /actions\/setup-node@v4/);
  assert.match(workflow, /npm ci/);
  assert.match(workflow, /npm test/);
  assert.match(workflow, /npm run build/);
  assert.match(workflow, /VCPKG_DEFAULT_BINARY_CACHE/);
  assert.match(workflow, /actions\/cache\/(?:restore|save)@v4/);
  assert.match(workflow, /--triplet arm64-osx/);
  assert.match(workflow, /builtin-baseline/);
  assert.match(workflow, /git -C \"\$root\" cat-file -e/);
  assert.match(workflow, /git -C \"\$root\" fetch --no-tags/);
  assert.match(workflow, /Unable to materialize vcpkg builtin-baseline/);
  assert.match(workflow, /cmake --preset macos-arm64/);
  assert.match(workflow, /cmake --build --preset macos-arm64-release/);
});

test("GUI and Metal tests are explicit required gates", () => {
  assert.match(workflow, /-E \"\$GUI_TEST_REGEX\"/);
  assert.match(workflow, /-N -R \"\$GUI_TEST_REGEX\"/);
  assert.match(workflow, /No macOS GUI\/Metal tests were discovered/);
  assert.match(workflow, /-R \"\$GUI_TEST_REGEX\"/);
  assert.match(workflow, /MTL_DEBUG_LAYER: \"1\"/);
  assert.match(workflow, /git diff --check/);
});
