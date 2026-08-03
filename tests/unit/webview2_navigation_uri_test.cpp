#include "platform/windows/webview2_navigation_uri.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>

namespace {

std::wstring canonicalDocument(std::wstring_view uri) {
  std::wstring document;
  EXPECT_EQ(canvas::windows::detail::canonicalDocumentUri(uri, document),
            S_OK);
  return document;
}

TEST(WebView2NavigationUri, CanonicalizesEquivalentAbsoluteDocumentUris) {
  EXPECT_EQ(canonicalDocument(L"HTTPS://EXAMPLE.TEST:443"),
            canonicalDocument(L"https://example.test/"));
  EXPECT_EQ(canonicalDocument(L"https://example.test/a/./b/../c"),
            canonicalDocument(L"https://example.test/a/c"));
  EXPECT_EQ(canonicalDocument(L"https://example.test/%7euser"),
            canonicalDocument(L"https://example.test/~user"));
}

TEST(WebView2NavigationUri, ExcludesFragmentsButPreservesDocumentDifferences) {
  EXPECT_EQ(canonicalDocument(L"https://example.test/page#old"),
            canonicalDocument(L"https://example.test/page#new"));
  EXPECT_NE(canonicalDocument(L"https://example.test/page?version=1#old"),
            canonicalDocument(L"https://example.test/page?version=2#new"));
  EXPECT_NE(canonicalDocument(L"https://example.test:444/page#old"),
            canonicalDocument(L"https://example.test/page#new"));
}

TEST(WebView2NavigationUri,
     DataDocumentIdentityIsComFreeAndOnlyDropsTheFragment) {
  EXPECT_EQ(canonicalDocument(L"data:text/html,first#old"),
            L"data:text/html,first");
  EXPECT_EQ(canonicalDocument(L"DATA:text/html,first#new"),
            L"data:text/html,first");
  EXPECT_NE(canonicalDocument(L"data:text/html,first#old"),
            canonicalDocument(L"data:text/html,second#new"));
}

TEST(WebView2NavigationUri, FragmentDetectionDoesNotTreatEscapedHashAsDelimiter) {
  EXPECT_TRUE(canvas::windows::detail::navigationUriHasFragment(
      L"https://example.test/page#section"));
  EXPECT_FALSE(canvas::windows::detail::navigationUriHasFragment(
      L"https://example.test/page%23section"));
}

TEST(WebView2NavigationUri, FailureDoesNotOverwriteThePreviousIdentity) {
  const std::wstring invalid(L"https://example.test/\0tail", 26U);
  std::wstring document = L"unchanged";
  EXPECT_EQ(canvas::windows::detail::canonicalDocumentUri(invalid, document),
            E_INVALIDARG);
  EXPECT_EQ(document, L"unchanged");
}

TEST(WebView2NavigationUri,
     OversizedInputIsRejectedBeforeUrlMonAndPreservesTheOutput) {
  const std::wstring oversized(
      canvas::windows::detail::kWebView2MaxNavigationCodeUnits + 1U, L'x');
  std::wstring document = L"unchanged";
  EXPECT_EQ(
      canvas::windows::detail::canonicalDocumentUri(oversized, document),
      E_INVALIDARG);
  EXPECT_EQ(document, L"unchanged");
}

}  // namespace
