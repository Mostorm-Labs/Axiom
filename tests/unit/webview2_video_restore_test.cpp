#include "platform/windows/webview2_video_restore.h"

#include <gtest/gtest.h>

#include <string>

namespace {

using canvas::windows::detail::PersistedVideoSourceClass;
using canvas::windows::detail::VideoRestoreMediaAction;
using canvas::windows::detail::buildSetVideoSourceMessage;
using canvas::windows::detail::buildVideoRestorePlan;
using canvas::windows::detail::classifyPersistedVideoSource;

TEST(WebView2VideoRestore, ClassifiesPackagedRemoteLocalAndVirtualSources) {
  EXPECT_EQ(classifyPersistedVideoSource(
                L"https://canvas.local/video.html?nodeId=video-1"),
            PersistedVideoSourceClass::PackagedAdapter);
  EXPECT_EQ(classifyPersistedVideoSource(
                L"https://cdn.example:8443/video.mp4?quality=high"),
            PersistedVideoSourceClass::RemoteHttps);
  EXPECT_EQ(classifyPersistedVideoSource(L"C:\\Media\\video.mp4"),
            PersistedVideoSourceClass::LocalFileCandidate);
  EXPECT_EQ(
      classifyPersistedVideoSource(L"https://media.canvas.local/video.mp4"),
      PersistedVideoSourceClass::UnrestorableVirtualHost);
  EXPECT_EQ(classifyPersistedVideoSource(
                L"https://media.canvas.local:443/video.mp4"),
            PersistedVideoSourceClass::UnrestorableVirtualHost);
}

TEST(WebView2VideoRestore, MirrorsThePackagedVideoSourcePolicy) {
  for (const std::wstring_view rejected : {
           L"https://user:secret@cdn.example/video.mp4",
           L"https://media.canvas.local@attacker.example/video.mp4",
           L"https://canvas.local.attacker.example/video.mp4",
           L"https://media.canvas.local.attacker.example/video.mp4",
           L"https://canvas.local%2Eattacker.example/video.mp4",
           L"https://canvas.local\u3002attacker.example/video.mp4",
           L"https://canva\u017F.local.attacker.example/video.mp4",
           L"https://\u24D2\u24D0\u24DD\u24E5\u24D0\u24E2.local.attacker.example/"
           L"video.mp4",
           L"https://canvas.local/not-the-video-adapter.html",
           L"https://cdn.example:/video.mp4",
           L"https://cdn.example:0/video.mp4",
           L"https://cdn.example:65536/video.mp4",
           L"https://999.999.999.999/video.mp4",
           L"https://127.0.0.1/video.mp4",
           L"https://example.123/video.mp4",
           L"https://example.0x10/video.mp4",
           L"https://xn--canvas-9za.example/video.mp4",
           L"http://cdn.example/video.mp4",
           L"file:///C:/Media/video.mp4",
           L"data:video/mp4;base64,AAAA",
           L"\\\\attacker\\share\\video.mp4",
           L"\\\\.\\pipe\\video.mp4",
           L"\\\\?\\C:\\Media\\video.mp4",
       }) {
    EXPECT_EQ(classifyPersistedVideoSource(rejected),
              PersistedVideoSourceClass::Rejected);
  }
}

TEST(WebView2VideoRestore, ValidatesTheProcessLocalMediaUrlShape) {
  EXPECT_EQ(classifyPersistedVideoSource(
                L"https://media.canvas.local/demo%20clip.mp4"),
            PersistedVideoSourceClass::UnrestorableVirtualHost);
  for (const std::wstring_view rejected : {
           L"https://media.canvas.local:444/video.mp4",
           L"https://media.canvas.local/video.mp4?token=1",
           L"https://media.canvas.local/folder/video.mp4",
           L"https://media.canvas.local/folder%2Fvideo.mp4",
           L"https://media.canvas.local/%FF.mp4",
       }) {
    EXPECT_EQ(classifyPersistedVideoSource(rejected),
              PersistedVideoSourceClass::Rejected);
  }
}

TEST(WebView2VideoRestore, ValidatesExplicitPortsWithoutRelyingOnShlwapi) {
  using canvas::windows::detail::video_restore_detail::HttpsPortClass;
  using canvas::windows::detail::video_restore_detail::classifyHttpsPort;
  EXPECT_EQ(classifyHttpsPort(L""), HttpsPortClass::Invalid);
  EXPECT_EQ(classifyHttpsPort(L"0"), HttpsPortClass::Invalid);
  EXPECT_EQ(classifyHttpsPort(L"443"), HttpsPortClass::EmptyOrDefault);
  EXPECT_EQ(classifyHttpsPort(L"8443"), HttpsPortClass::NonDefault);
  EXPECT_EQ(classifyHttpsPort(L"65535"), HttpsPortClass::NonDefault);
  EXPECT_EQ(classifyHttpsPort(L"65536"), HttpsPortClass::Invalid);
}

TEST(WebView2VideoRestore, AlwaysBuildsThePackagedAdapterNavigation) {
  const auto remote = buildVideoRestorePlan(
      L"video id/1", L"https://cdn.example/video.mp4");
  ASSERT_TRUE(remote.has_value());
  EXPECT_EQ(remote->sourceClass, PersistedVideoSourceClass::RemoteHttps);
  EXPECT_EQ(remote->mediaAction,
            VideoRestoreMediaAction::UsePersistedRemote);
  EXPECT_EQ(remote->navigationUri,
            L"https://canvas.local/video.html?nodeId=video%20id%2F1");
  ASSERT_TRUE(remote->initialMessage.has_value());
  EXPECT_EQ(*remote->initialMessage,
            L"{\"protocolVersion\":1,\"type\":\"set-video-source\","
            L"\"nodeId\":\"video id/1\",\"payload\":{\"source\":"
            L"\"https://cdn.example/video.mp4\"}}");

  const auto packaged = buildVideoRestorePlan(
      L"video-1", L"https://canvas.local/video.html?nodeId=old-id");
  ASSERT_TRUE(packaged.has_value());
  EXPECT_EQ(packaged->sourceClass,
            PersistedVideoSourceClass::PackagedAdapter);
  EXPECT_EQ(packaged->mediaAction, VideoRestoreMediaAction::None);
  EXPECT_EQ(packaged->navigationUri,
            L"https://canvas.local/video.html?nodeId=video-1");
  EXPECT_FALSE(packaged->initialMessage.has_value());

  const auto unicodeNode = buildVideoRestorePlan(
      L"\u89C6\u9891", L"https://canvas.local/video.html");
  ASSERT_TRUE(unicodeNode.has_value());
  EXPECT_EQ(unicodeNode->navigationUri,
            L"https://canvas.local/video.html?nodeId="
            L"%E8%A7%86%E9%A2%91");
}

TEST(WebView2VideoRestore, PlansApprovalAndRefusalWithoutOpeningFiles) {
  const auto local =
      buildVideoRestorePlan(L"video-1", L"C:\\Media\\video.mp4");
  ASSERT_TRUE(local.has_value());
  EXPECT_EQ(local->sourceClass,
            PersistedVideoSourceClass::LocalFileCandidate);
  EXPECT_EQ(local->mediaAction,
            VideoRestoreMediaAction::ApprovePersistedLocalFile);
  EXPECT_FALSE(local->initialMessage.has_value());

  const auto virtualHost = buildVideoRestorePlan(
      L"video-1", L"https://media.canvas.local/video.mp4");
  ASSERT_TRUE(virtualHost.has_value());
  EXPECT_EQ(virtualHost->sourceClass,
            PersistedVideoSourceClass::UnrestorableVirtualHost);
  EXPECT_EQ(virtualHost->mediaAction, VideoRestoreMediaAction::Reject);

  const auto invalid = buildVideoRestorePlan(
      L"video-1", L"https://user:secret@cdn.example/video.mp4");
  ASSERT_TRUE(invalid.has_value());
  EXPECT_EQ(invalid->sourceClass, PersistedVideoSourceClass::Rejected);
  EXPECT_EQ(invalid->mediaAction, VideoRestoreMediaAction::Reject);
}

TEST(WebView2VideoRestore, EscapesProtocolMessagesAsJson) {
  const auto message = buildSetVideoSourceMessage(
      L"video-\"\\\n", L"https://cdn.example/a\"\\\t.mp4");
  ASSERT_TRUE(message.has_value());
  EXPECT_EQ(*message,
            L"{\"protocolVersion\":1,\"type\":\"set-video-source\","
            L"\"nodeId\":\"video-\\\"\\\\\\n\",\"payload\":{"
            L"\"source\":\"https://cdn.example/a\\\"\\\\\\t.mp4\"}}");
}

TEST(WebView2VideoRestore, RejectsInvalidPlanAndMessageInputs) {
  EXPECT_FALSE(buildVideoRestorePlan(
                   L"", L"https://cdn.example/video.mp4")
                   .has_value());
  EXPECT_FALSE(buildSetVideoSourceMessage(L"video-1", L"").has_value());
  const std::wstring oversized(
      canvas::windows::detail::kWebView2MaxMessageCodeUnits, L'x');
  EXPECT_FALSE(buildSetVideoSourceMessage(L"video-1", oversized).has_value());
}

}  // namespace
