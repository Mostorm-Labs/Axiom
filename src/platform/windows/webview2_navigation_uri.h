#pragma once

#include "platform/windows/webview2_message_log.h"

#include <windows.h>
#include <oleauto.h>
#include <urlmon.h>
#include <wrl/client.h>

#include <string>
#include <string_view>

namespace canvas::windows::detail {

inline bool navigationUriHasScheme(std::wstring_view uri,
                                   std::wstring_view scheme) noexcept {
  if (uri.size() <= scheme.size() || uri[scheme.size()] != L':') {
    return false;
  }
  for (std::size_t index = 0; index < scheme.size(); ++index) {
    wchar_t left = uri[index];
    wchar_t right = scheme[index];
    if (left >= L'A' && left <= L'Z') {
      left = static_cast<wchar_t>(left + (L'a' - L'A'));
    }
    if (right >= L'A' && right <= L'Z') {
      right = static_cast<wchar_t>(right + (L'a' - L'A'));
    }
    if (left != right) return false;
  }
  return true;
}

inline bool navigationUriHasFragment(std::wstring_view uri) noexcept {
  return uri.find(L'#') != std::wstring_view::npos;
}

// Data URLs are enabled only for deterministic WebView2 integration tests.
// URLMon does not promise COM-free parsing for opaque/unknown schemes, while
// the test fixture intentionally permits navigate() before COM initialization
// (for example, to verify close cancellation).  Their document identity is
// still unambiguous for our purpose: retain the bytes before the fragment and
// do not attempt to decode the payload.
inline HRESULT copyOpaqueDocumentUri(std::wstring_view uri,
                                     std::wstring& documentKey) noexcept {
  if (uri.empty() || uri.size() > kWebView2MaxNavigationCodeUnits ||
      uri.find(L'\0') != std::wstring_view::npos) {
    return E_INVALIDARG;
  }
  const auto fragment = uri.find(L'#');
  const auto documentLength = fragment == std::wstring_view::npos
                                  ? uri.size()
                                  : fragment;
  try {
    std::wstring copy(uri.substr(0, documentLength));
    // URI schemes are ASCII case-insensitive; leave the data payload byte
    // sequence untouched because it can be case-sensitive.
    const auto schemeEnd = copy.find(L':');
    const auto schemeLength = schemeEnd == std::wstring::npos
                                  ? std::size_t{0U}
                                  : schemeEnd;
    for (std::size_t index = 0U;
         index < schemeLength && index < copy.size(); ++index) {
      if (copy[index] >= L'A' && copy[index] <= L'Z') {
        copy[index] = static_cast<wchar_t>(copy[index] + (L'a' - L'A'));
      }
    }
    documentKey.swap(copy);
  } catch (...) {
    return E_OUTOFMEMORY;
  }
  return S_OK;
}

// URLMon applies the same Windows URI canonicalization rules to both the
// requested URI and WebView2's committed Source. The fragment is deliberately
// excluded because WebView2 does not raise NavigationStarting for a provable
// same-document fragment navigation. Output changes only after full success.
inline HRESULT canonicalDocumentUri(std::wstring_view uri,
                                    std::wstring& documentKey) noexcept {
  if (uri.empty() || uri.size() > kWebView2MaxNavigationCodeUnits ||
      uri.find(L'\0') != std::wstring_view::npos) {
    return E_INVALIDARG;
  }

  // Keep this path independent of COM.  It is used only with the opt-in test
  // data scheme, and it also lets a caller queue a data URL before the WebView
  // controller (or even COM itself) exists.
  if (navigationUriHasScheme(uri, L"data")) {
    return copyOpaqueDocumentUri(uri, documentKey);
  }

  std::wstring ownedUri;
  try {
    ownedUri.assign(uri);
  } catch (...) {
    return E_OUTOFMEMORY;
  }

  Microsoft::WRL::ComPtr<IUri> parsedUri;
  HRESULT result = CreateUri(
      ownedUri.c_str(), Uri_CREATE_CANONICALIZE | Uri_CREATE_NO_IE_SETTINGS,
      0U, parsedUri.GetAddressOf());
  if (FAILED(result)) return result;
  if (!parsedUri) return E_POINTER;

  BSTR absoluteUri = nullptr;
  result = parsedUri->GetAbsoluteUri(&absoluteUri);
  if (FAILED(result) || absoluteUri == nullptr) {
    SysFreeString(absoluteUri);
    return FAILED(result) ? result : E_POINTER;
  }

  const auto releaseAbsoluteUri = [&absoluteUri]() noexcept {
    if (absoluteUri == nullptr) return;
    SysFreeString(absoluteUri);
    absoluteUri = nullptr;
  };

  try {
    std::wstring canonical(absoluteUri,
                           static_cast<std::size_t>(SysStringLen(absoluteUri)));
    releaseAbsoluteUri();
    const auto fragment = canonical.find(L'#');
    if (fragment != std::wstring::npos) canonical.resize(fragment);
    documentKey.swap(canonical);
  } catch (...) {
    releaseAbsoluteUri();
    return E_OUTOFMEMORY;
  }
  return S_OK;
}

}  // namespace canvas::windows::detail
