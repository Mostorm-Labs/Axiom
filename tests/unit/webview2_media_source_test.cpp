#include "platform/windows/webview2_media_source.h"

#include <gtest/gtest.h>

#include <string>

namespace {

TEST(WebView2MediaSource, EncodesOneUtf8FilenameAndMapsOnlyItsParent) {
  canvas::windows::detail::LocalMediaSource source;
  ASSERT_EQ(canvas::windows::detail::deriveLocalMediaSource(
                L"C:\\Canvas Tests\\media\\\u6F14\u793A clip #1.mp4", source),
            S_OK);
  EXPECT_EQ(source.folder, L"C:\\Canvas Tests\\media");
  EXPECT_EQ(source.uri,
            L"https://media.canvas.local/"
            L"%E6%BC%94%E7%A4%BA%20clip%20%231.mp4");
}

TEST(WebView2MediaSource, RejectsFilesWhoseParentIsAnArbitraryRoot) {
  canvas::windows::detail::LocalMediaSource source;
  EXPECT_EQ(canvas::windows::detail::deriveLocalMediaSource(
                L"C:\\root-video.mp4", source),
            E_ACCESSDENIED);
  EXPECT_EQ(canvas::windows::detail::deriveLocalMediaSource(
                L"\\\\server\\share\\root-video.mp4", source),
            E_ACCESSDENIED);
  EXPECT_EQ(canvas::windows::detail::deriveLocalMediaSource(
                L"\\\\server\\root-video.mp4", source),
            E_ACCESSDENIED);
}

TEST(WebView2MediaSource, CanonicalHandlePathsExposeJunctionTargetsForReview) {
  std::wstring resolved;
  ASSERT_EQ(canvas::windows::detail::dosPathFromFinalHandlePath(
                L"\\\\?\\C:\\root-video.mp4", resolved),
            S_OK);
  EXPECT_EQ(resolved, L"C:\\root-video.mp4");

  canvas::windows::detail::LocalMediaSource source;
  EXPECT_EQ(canvas::windows::detail::deriveLocalMediaSource(resolved, source),
            E_ACCESSDENIED);
}

TEST(WebView2MediaSource, RejectsRelativeDirectoriesAndNonFilePaths) {
  canvas::windows::detail::LocalMediaSource source;
  EXPECT_EQ(canvas::windows::detail::deriveLocalMediaSource(
                L"relative\\video.mp4", source),
            E_INVALIDARG);
  EXPECT_EQ(canvas::windows::detail::deriveLocalMediaSource(
                L"C:\\Canvas Tests\\media\\", source),
            E_INVALIDARG);
  EXPECT_EQ(canvas::windows::detail::deriveLocalMediaSource(
                L"C:\\Canvas Tests\\media\\..", source),
            E_INVALIDARG);
  EXPECT_EQ(canvas::windows::detail::deriveLocalMediaSource(
                L"C:\\Canvas Tests\\media\\notes.txt", source),
            HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED));
}

}  // namespace
