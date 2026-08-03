import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";

const repositoryRoot = resolve(dirname(fileURLToPath(import.meta.url)), "../..");

function readRepositoryFile(relativePath) {
  try {
    return readFileSync(resolve(repositoryRoot, relativePath), "utf8");
  } catch (error) {
    if (error?.code === "ENOENT") {
      return "";
    }
    throw error;
  }
}

const workflow = readRepositoryFile(".github/workflows/windows-build.yml");
const packager = readRepositoryFile("scripts/New-WindowsPortablePackage.ps1");
const windowsBehaviorContract = readRepositoryFile(
  "tests/contracts/Test-WindowsPortablePackage.ps1",
);
const packageReadme = readRepositoryFile("packaging/windows/README.txt");
const projectReadme = readRepositoryFile("README.md");
const workflowLines = workflow.replaceAll("\r\n", "\n").split("\n");

function jobBlock(name) {
  const jobsLine = workflowLines.indexOf("jobs:");
  assert.notEqual(jobsLine, -1, "workflow is missing jobs");
  const start = workflowLines.findIndex(
    (line, index) => index > jobsLine && line === `  ${name}:`,
  );
  assert.notEqual(start, -1, `missing workflow job: ${name}`);
  const nextJobOffset = workflowLines
    .slice(start + 1)
    .findIndex((line) => /^  [A-Za-z0-9_-]+:\s*$/.test(line));
  const end = nextJobOffset === -1 ? workflowLines.length : start + 1 + nextJobOffset;
  return workflowLines.slice(start, end).join("\n");
}

function namedStepBlock(job, name) {
  const lines = job.split("\n");
  const start = lines.findIndex((line) => line === `      - name: ${name}`);
  assert.notEqual(start, -1, `missing workflow step: ${name}`);
  const nextStepOffset = lines
    .slice(start + 1)
    .findIndex((line) => /^      - (?:name|uses):/.test(line));
  const end = nextStepOffset === -1 ? lines.length : start + 1 + nextStepOffset;
  return lines.slice(start, end).join("\n");
}

function stepPosition(job, name) {
  const marker = `      - name: ${name}\n`;
  const position = `${job}\n`.indexOf(marker);
  assert.notEqual(position, -1, `missing workflow step: ${name}`);
  return position;
}

const buildJob = jobBlock("build");
const releaseJob = jobBlock("release");

test("Windows workflow handles PR, manual, and v-tag builds with read-only defaults", () => {
  assert.match(workflow, /^  pull_request:\s*$/m);
  assert.match(workflow, /^  workflow_dispatch:\s*$/m);
  assert.match(workflow, /^  push:\s*\n    tags:\s*\n      - ["']v\*["']\s*$/m);
  assert.match(workflow, /^permissions:\s*\n  contents: read\s*$/m);
});

test("portable artifact is created only after every Windows validation gate", () => {
  const packagePosition = stepPosition(buildJob, "Package Windows portable bundle");
  const uploadPosition = stepPosition(buildJob, "Upload Windows portable artifact");

  for (const gate of [
    "Build",
    "Test",
    "Composition integration tests",
    "Release packaging contract",
    "Whitespace check",
  ]) {
    assert.ok(stepPosition(buildJob, gate) < packagePosition, `${gate} must precede packaging`);
  }
  assert.ok(packagePosition < uploadPosition, "packaging must precede artifact upload");
  const contractStep = namedStepBlock(buildJob, "Release packaging contract");
  const uploadStep = namedStepBlock(buildJob, "Upload Windows portable artifact");
  assert.match(contractStep, /\.\/tests\/contracts\/Test-WindowsPortablePackage\.ps1/);
  assert.match(uploadStep, /uses: actions\/upload-artifact@v4/);
  assert.match(uploadStep, /retention-days: 30/);
  assert.match(uploadStep, /if-no-files-found: error/);
  assert.doesNotMatch(buildJob, /^\s+continue-on-error:/m);
});

test("release waits for the build and alone receives contents write permission", () => {
  assert.match(
    releaseJob,
    /^  release:\s*\n    if: startsWith\(github\.ref, 'refs\/tags\/v'\)\s*\n    needs: build\s*\n    permissions:\s*\n      contents: write\s*$/m,
  );
  const downloadStep = namedStepBlock(releaseJob, "Download tested Windows package");
  const publishStep = namedStepBlock(releaseJob, "Create or update GitHub Release");
  assert.match(downloadStep, /uses: actions\/download-artifact@v4/);
  assert.match(publishStep, /GH_REPO: \$\{\{ github\.repository \}\}/);
  assert.match(publishStep, /gh release edit[\s\S]*--draft=false/);
  assert.match(publishStep, /gh release upload[^\n]*--clobber/);
  assert.match(publishStep, /\*-/);
  assert.doesNotMatch(releaseJob, /pull_request/);
  assert.doesNotMatch(releaseJob, /^\s+continue-on-error:/m);
  assert.equal(
    (workflow.match(/contents: write/g) ?? []).length,
    1,
    "only the release job may request contents:write",
  );
});

test("build outputs pair the uploaded artifact with the release download", () => {
  const uploadStep = namedStepBlock(buildJob, "Upload Windows portable artifact");
  const downloadStep = namedStepBlock(releaseJob, "Download tested Windows package");
  const publishStep = namedStepBlock(releaseJob, "Create or update GitHub Release");

  assert.match(
    buildJob,
    /package-artifact-name: \$\{\{ steps\.package-metadata\.outputs\.artifact-name \}\}/,
  );
  assert.match(
    uploadStep,
    /name: \$\{\{ steps\.package-metadata\.outputs\.artifact-name \}\}/,
  );
  assert.match(
    downloadStep,
    /name: \$\{\{ needs\.build\.outputs\.package-artifact-name \}\}/,
  );
  for (const output of ["archive", "checksum"]) {
    assert.match(
      buildJob,
      new RegExp(
        `package-${output}-name: \\$\\{\\{ steps\\.package-metadata\\.outputs\\.${output}-name \\}\\}`,
      ),
    );
    assert.match(
      publishStep,
      new RegExp(
        `${output.toUpperCase()}_NAME: \\$\\{\\{ needs\\.build\\.outputs\\.package-${output}-name \\}\\}`,
      ),
    );
  }
});

test("packager validates and emits a complete versioned portable bundle", () => {
  for (const requiredToken of [
    "canvas_windows.exe",
    "richtext.html",
    "video.html",
    "README.txt",
    "Get-FileHash",
    "Compress-Archive",
    ".sha256",
    "Assert-NotReparsePoint",
    "Assert-PathsDoNotOverlap",
    "OrdinalIgnoreCase",
  ]) {
    assert.ok(packager.includes(requiredToken), `packager is missing ${requiredToken}`);
  }
  assert.match(packager, /A-Za-z0-9\._-/);
  assert.match(packager, /\.Length -le 0/);
  assert.match(packager, /Assert-NotReparsePoint \$BuildDirectory 'Build directory'/);
  assert.match(packager, /Assert-NonEmptyFile \$ReadmePath 'Portable-package README'/);
  assert.match(packager, /Assert-NonEmptyFile \$executableSource 'Canvas executable'/);
  assert.match(packager, /Assert-NotReparsePoint \$webSource 'Embedded web asset directory'/);
  assert.match(packager, /Assert-PathsDoNotOverlap \$outputRoot \$buildRoot/);
  assert.match(packager, /Assert-PathsDoNotOverlap \$outputRoot \$webSource/);
  assert.match(packager, /Assert-PathsDoNotOverlap \$outputRoot \$readmeSource/);
  assert.match(packager, /Get-ChildItem -LiteralPath \$bundleRoot -Recurse -Force/);
});

test("Windows behavior contract covers late cleanup, overlap, and junction failures", () => {
  for (const requiredToken of [
    "Assert-NoPackageResidue",
    "empty.css",
    "Post-archive validation failure",
    "Descendant-overlap failure",
    "Ancestor-overlap failure",
    "Case-insensitive overlap failure",
    "README-overlap failure",
    "-ItemType Junction",
    "Junction-source failure",
    ".canvas-package-*",
  ]) {
    assert.ok(
      windowsBehaviorContract.includes(requiredToken),
      `Windows behavior contract is missing ${requiredToken}`,
    );
  }
});

test("download documentation states the portable runtime boundaries", () => {
  assert.match(packageReadme, /native portable/i);
  assert.match(packageReadme, /WebView2 Runtime/i);
  assert.match(packageReadme, /web[\\/] directory/i);
  assert.match(packageReadme, /Electron launcher is not included/i);
  assert.match(projectReadme, /GitHub Actions/i);
  assert.match(projectReadme, /GitHub Releases/i);
});
