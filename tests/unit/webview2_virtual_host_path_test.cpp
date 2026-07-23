#include "platform/windows/webview2_virtual_host_path.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

std::wstring currentDirectory() {
  const DWORD required = GetCurrentDirectoryW(0, nullptr);
  if (required == 0) return {};
  std::vector<wchar_t> buffer(required);
  const DWORD written =
      GetCurrentDirectoryW(static_cast<DWORD>(buffer.size()), buffer.data());
  return written == 0 ? std::wstring{} : std::wstring(buffer.data(), written);
}

class ScopedCurrentDirectory final {
 public:
  ScopedCurrentDirectory() : original_(currentDirectory()) {}
  ~ScopedCurrentDirectory() {
    if (!original_.empty()) SetCurrentDirectoryW(original_.c_str());
  }

 private:
  std::wstring original_;
};

TEST(WebView2VirtualHostPath, RelativeFolderIsIndependentOfCurrentDirectory) {
  ScopedCurrentDirectory restore;
  wchar_t windowsDirectory[MAX_PATH]{};
  ASSERT_NE(GetWindowsDirectoryW(windowsDirectory, MAX_PATH), 0U);
  wchar_t temporaryDirectory[MAX_PATH]{};
  ASSERT_NE(GetTempPathW(MAX_PATH, temporaryDirectory), 0U);

  std::wstring fromWindowsDirectory;
  ASSERT_TRUE(SetCurrentDirectoryW(windowsDirectory));
  ASSERT_EQ(canvas::windows::detail::normalizeVirtualHostFolder(
                L"assets\\web", L"C:\\Program Files\\Mostorm\\Canvas",
                fromWindowsDirectory),
            S_OK);

  std::wstring fromTemporaryDirectory;
  ASSERT_TRUE(SetCurrentDirectoryW(temporaryDirectory));
  ASSERT_EQ(canvas::windows::detail::normalizeVirtualHostFolder(
                L"assets\\web", L"C:\\Program Files\\Mostorm\\Canvas",
                fromTemporaryDirectory),
            S_OK);

  EXPECT_EQ(fromWindowsDirectory, fromTemporaryDirectory);
  EXPECT_EQ(fromWindowsDirectory,
            L"C:\\Program Files\\Mostorm\\Canvas\\assets\\web");
}

TEST(WebView2VirtualHostPath, AbsoluteFolderDoesNotUseExecutableDirectory) {
  std::wstring normalized;
  EXPECT_EQ(canvas::windows::detail::normalizeVirtualHostFolder(
                L"D:\\CanvasData\\.\\web\\..\\media", L"C:\\Canvas",
                normalized),
            S_OK);
  EXPECT_EQ(normalized, L"D:\\CanvasData\\media");
}

TEST(WebView2VirtualHostPath, RejectsAmbiguousDriveRelativePaths) {
  std::wstring normalized;
  EXPECT_EQ(canvas::windows::detail::normalizeVirtualHostFolder(
                L"C:assets", L"C:\\Canvas", normalized),
            E_INVALIDARG);
  EXPECT_TRUE(normalized.empty());
  EXPECT_EQ(canvas::windows::detail::normalizeVirtualHostFolder(
                L"\\assets", L"C:\\Canvas", normalized),
            E_INVALIDARG);
  EXPECT_TRUE(normalized.empty());
}

TEST(WebView2VirtualHostPath, PreservesDriveRootForRootLevelExecutable) {
  std::wstring directory;
  EXPECT_EQ(canvas::windows::detail::directoryFromExecutablePath(
                L"C:\\canvas.exe", directory),
            S_OK);
  EXPECT_EQ(directory, L"C:\\");

  std::wstring normalized;
  EXPECT_EQ(canvas::windows::detail::normalizeVirtualHostFolder(
                L"assets", directory, normalized),
            S_OK);
  EXPECT_EQ(normalized, L"C:\\assets");
}

TEST(WebView2VirtualHostPath, EnforcesWebViewFolderPathMaxPathBoundary) {
  std::wstring allowed = L"C:\\";
  allowed.append(256, L'a');
  ASSERT_EQ(allowed.size(), static_cast<std::size_t>(MAX_PATH - 1));
  std::wstring normalized;
  EXPECT_EQ(canvas::windows::detail::normalizeVirtualHostFolder(
                allowed, L"D:\\Canvas", normalized),
            S_OK);
  EXPECT_EQ(normalized.size(), static_cast<std::size_t>(MAX_PATH - 1));

  std::wstring rejected = allowed + L'a';
  ASSERT_EQ(rejected.size(), static_cast<std::size_t>(MAX_PATH));
  EXPECT_EQ(canvas::windows::detail::normalizeVirtualHostFolder(
                rejected, L"D:\\Canvas", normalized),
            HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE));
  EXPECT_TRUE(normalized.empty());
}

}  // namespace
