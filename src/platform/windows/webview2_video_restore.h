#pragma once

#include "platform/windows/webview2_message_log.h"
#include "platform/windows/webview2_surface.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace canvas::windows::detail {

// Keep the persisted-source decision a conservative subset of
// web/src/video-source.ts. Native accepts only canonical ASCII DNS hosts, so a
// URL can never pass here and then become a reserved-host lookalike after the
// packaged page applies WHATWG/IDNA normalization. PackagedAdapter is a
// persistence compatibility value, not a media URL: the page itself is
// restored but no set-video-source message is sent for it.
enum class PersistedVideoSourceClass {
  PackagedAdapter,
  RemoteHttps,
  LocalFileCandidate,
  UnrestorableVirtualHost,
  Rejected,
};

enum class VideoRestoreMediaAction {
  None,
  UsePersistedRemote,
  ApprovePersistedLocalFile,
  Reject,
};

struct VideoRestorePlan {
  PersistedVideoSourceClass sourceClass =
      PersistedVideoSourceClass::Rejected;
  VideoRestoreMediaAction mediaAction = VideoRestoreMediaAction::Reject;
  std::wstring navigationUri;
  std::optional<std::wstring> initialMessage;
};

namespace video_restore_detail {

struct HttpsUrlParts {
  std::wstring_view host;
  std::wstring_view path;
  bool hasCredentials = false;
  bool hasInvalidPort = false;
  bool hasNonDefaultPort = false;
  bool hasSearchOrHash = false;
};

inline bool asciiEqualsIgnoreCase(std::wstring_view left,
                                  std::wstring_view right) noexcept {
  if (left.size() != right.size()) return false;
  for (std::size_t index = 0; index < left.size(); ++index) {
    wchar_t leftCharacter = left[index];
    wchar_t rightCharacter = right[index];
    if (leftCharacter >= L'A' && leftCharacter <= L'Z') {
      leftCharacter = static_cast<wchar_t>(leftCharacter - L'A' + L'a');
    }
    if (rightCharacter >= L'A' && rightCharacter <= L'Z') {
      rightCharacter =
          static_cast<wchar_t>(rightCharacter - L'A' + L'a');
    }
    if (leftCharacter != rightCharacter) return false;
  }
  return true;
}

inline bool asciiStartsWithIgnoreCase(std::wstring_view value,
                                      std::wstring_view prefix) noexcept {
  return value.size() >= prefix.size() &&
         asciiEqualsIgnoreCase(value.substr(0, prefix.size()), prefix);
}

enum class HttpsPortClass { EmptyOrDefault, NonDefault, Invalid };

inline HttpsPortClass classifyHttpsPort(std::wstring_view port) noexcept {
  if (port.empty()) return HttpsPortClass::Invalid;
  unsigned int value = 0U;
  for (const wchar_t character : port) {
    if (character < L'0' || character > L'9') {
      return HttpsPortClass::Invalid;
    }
    const auto digit = static_cast<unsigned int>(character - L'0');
    if (value > (65535U - digit) / 10U) {
      return HttpsPortClass::Invalid;
    }
    value = value * 10U + digit;
  }
  if (value == 0U) return HttpsPortClass::Invalid;
  return value == 443U ? HttpsPortClass::EmptyOrDefault
                       : HttpsPortClass::NonDefault;
}

inline std::optional<HttpsUrlParts> splitHttpsUrl(
    std::wstring_view source) noexcept {
  constexpr std::wstring_view kScheme = L"https://";
  if (!asciiStartsWithIgnoreCase(source, kScheme)) return std::nullopt;

  const std::size_t authorityStart = kScheme.size();
  const std::size_t authorityEnd =
      source.find_first_of(L"/\\?#", authorityStart);
  const std::size_t end = authorityEnd == std::wstring_view::npos
                              ? source.size()
                              : authorityEnd;
  std::wstring_view authority =
      source.substr(authorityStart, end - authorityStart);
  if (authority.empty()) return std::nullopt;

  HttpsUrlParts parts;
  const std::size_t userInfoEnd = authority.rfind(L'@');
  if (userInfoEnd != std::wstring_view::npos) {
    const auto userInfo = authority.substr(0, userInfoEnd);
    parts.hasCredentials = !userInfo.empty() && userInfo != L":";
    authority.remove_prefix(userInfoEnd + 1U);
  }
  if (authority.empty()) return std::nullopt;

  if (authority.front() == L'[') {
    const std::size_t closingBracket = authority.find(L']');
    if (closingBracket == std::wstring_view::npos) return std::nullopt;
    parts.host = authority.substr(0, closingBracket + 1U);
    const auto suffix = authority.substr(closingBracket + 1U);
    if (!suffix.empty()) {
      if (suffix.front() != L':') return std::nullopt;
      const auto portClass = classifyHttpsPort(suffix.substr(1U));
      parts.hasInvalidPort = portClass == HttpsPortClass::Invalid;
      parts.hasNonDefaultPort = portClass == HttpsPortClass::NonDefault;
    }
  } else {
    const std::size_t portSeparator = authority.rfind(L':');
    if (portSeparator == std::wstring_view::npos) {
      parts.host = authority;
    } else {
      parts.host = authority.substr(0, portSeparator);
      const auto port = authority.substr(portSeparator + 1U);
      const auto portClass = classifyHttpsPort(port);
      parts.hasInvalidPort = portClass == HttpsPortClass::Invalid;
      parts.hasNonDefaultPort = portClass == HttpsPortClass::NonDefault;
    }
  }
  if (parts.host.empty()) return std::nullopt;

  const std::wstring_view tail = source.substr(end);
  const std::size_t query = tail.find(L'?');
  const std::size_t fragment = tail.find(L'#');
  const std::size_t queryOrFragment = tail.find_first_of(L"?#");
  if (query != std::wstring_view::npos) {
    const std::size_t queryEnd =
        fragment == std::wstring_view::npos ? tail.size() : fragment;
    parts.hasSearchOrHash = query + 1U < queryEnd;
  }
  if (fragment != std::wstring_view::npos && fragment + 1U < tail.size()) {
    parts.hasSearchOrHash = true;
  }
  parts.path = tail.substr(0, queryOrFragment);
  return parts;
}

inline int hexValue(wchar_t character) noexcept {
  if (character >= L'0' && character <= L'9') {
    return static_cast<int>(character - L'0');
  }
  if (character >= L'A' && character <= L'F') {
    return static_cast<int>(character - L'A') + 10;
  }
  if (character >= L'a' && character <= L'f') {
    return static_cast<int>(character - L'a') + 10;
  }
  return -1;
}

inline bool appendWideAsUtf8(std::wstring_view value, std::string& output) {
  if (value.empty()) return true;
  const int sourceLength = static_cast<int>(value.size());
  if (sourceLength < 0 ||
      static_cast<std::size_t>(sourceLength) != value.size()) {
    return false;
  }
  const int utf8Length = WideCharToMultiByte(
      CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), sourceLength, nullptr, 0,
      nullptr, nullptr);
  if (utf8Length <= 0) return false;
  const std::size_t oldSize = output.size();
  output.resize(oldSize + static_cast<std::size_t>(utf8Length));
  return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                             sourceLength, output.data() + oldSize, utf8Length,
                             nullptr, nullptr) == utf8Length;
}

inline std::optional<std::wstring> decodeUriPathSegment(
    std::wstring_view encoded) {
  try {
    std::string utf8;
    utf8.reserve(encoded.size());
    std::size_t runStart = 0;
    for (std::size_t index = 0; index < encoded.size();) {
      if (encoded[index] != L'%') {
        ++index;
        continue;
      }
      if (!appendWideAsUtf8(encoded.substr(runStart, index - runStart), utf8) ||
          index + 2U >= encoded.size()) {
        return std::nullopt;
      }
      const int high = hexValue(encoded[index + 1U]);
      const int low = hexValue(encoded[index + 2U]);
      if (high < 0 || low < 0) return std::nullopt;
      utf8.push_back(static_cast<char>((high << 4) | low));
      index += 3U;
      runStart = index;
    }
    if (!appendWideAsUtf8(encoded.substr(runStart), utf8) || utf8.empty()) {
      return std::nullopt;
    }
    const int utf8Length = static_cast<int>(utf8.size());
    if (utf8Length < 0 ||
        static_cast<std::size_t>(utf8Length) != utf8.size()) {
      return std::nullopt;
    }
    const int wideLength = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), utf8Length, nullptr, 0);
    if (wideLength <= 0) return std::nullopt;
    std::wstring decoded(static_cast<std::size_t>(wideLength), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
                            utf8Length, decoded.data(), wideLength) !=
        wideLength) {
      return std::nullopt;
    }
    return decoded;
  } catch (...) {
    return std::nullopt;
  }
}

inline std::optional<std::wstring> normalizeAsciiDnsHostname(
    std::wstring_view host) {
  if (host.empty() || host.size() > 253U) return std::nullopt;
  try {
    std::wstring normalized;
    normalized.reserve(host.size());
    std::size_t labelLength = 0U;
    bool previousWasHyphen = false;
    for (std::size_t index = 0; index < host.size(); ++index) {
      wchar_t character = host[index];
      if (character >= L'A' && character <= L'Z') {
        character = static_cast<wchar_t>(character - L'A' + L'a');
      }
      if (character == L'.') {
        const bool trailingDot = index + 1U == host.size();
        if (labelLength == 0U || previousWasHyphen) return std::nullopt;
        normalized.push_back(character);
        labelLength = 0U;
        previousWasHyphen = false;
        if (trailingDot) break;
        continue;
      }
      const bool letter = character >= L'a' && character <= L'z';
      const bool digit = character >= L'0' && character <= L'9';
      if (!letter && !digit && character != L'-') return std::nullopt;
      if ((labelLength == 0U && character == L'-') || labelLength >= 63U) {
        return std::nullopt;
      }
      normalized.push_back(character);
      ++labelLength;
      previousWasHyphen = character == L'-';
    }
    if (normalized.empty() || previousWasHyphen ||
        (labelLength == 0U && normalized.back() != L'.')) {
      return std::nullopt;
    }

    // This slice deliberately does not implement WHATWG IPv4-number or IDNA
    // canonicalization. Reject those syntaxes instead of risking a host that
    // Chromium later normalizes into a different security class.
    std::wstring_view normalizedView(normalized);
    if (normalizedView.back() == L'.') normalizedView.remove_suffix(1U);
    std::size_t labelStart = 0U;
    while (labelStart < normalizedView.size()) {
      const std::size_t labelEnd = normalizedView.find(L'.', labelStart);
      const std::size_t end = labelEnd == std::wstring_view::npos
                                  ? normalizedView.size()
                                  : labelEnd;
      const auto label = normalizedView.substr(labelStart, end - labelStart);
      if (label.size() >= 4U && label.substr(0, 4U) == L"xn--") {
        return std::nullopt;
      }
      if (labelEnd == std::wstring_view::npos) break;
      labelStart = labelEnd + 1U;
    }

    const std::size_t finalDot = normalizedView.rfind(L'.');
    const auto finalLabel = finalDot == std::wstring_view::npos
                                ? normalizedView
                                : normalizedView.substr(finalDot + 1U);
    bool hasAsciiLetter = false;
    for (const wchar_t character : finalLabel) {
      if (character >= L'a' && character <= L'z') {
        hasAsciiLetter = true;
        break;
      }
    }
    if (!hasAsciiLetter) return std::nullopt;
    if (finalLabel.size() > 2U && finalLabel[0] == L'0' &&
        finalLabel[1] == L'x') {
      bool allHex = true;
      for (std::size_t index = 2U; index < finalLabel.size(); ++index) {
        if (hexValue(finalLabel[index]) < 0) {
          allHex = false;
          break;
        }
      }
      if (allHex) return std::nullopt;
    }
    return normalized;
  } catch (...) {
    return std::nullopt;
  }
}

inline bool isDriveRootedLocalPath(std::wstring_view path) noexcept {
  if (path.size() < 3U || path[1] != L':' ||
      (path[2] != L'\\' && path[2] != L'/')) {
    return false;
  }
  const wchar_t drive = path.front();
  return (drive >= L'A' && drive <= L'Z') ||
         (drive >= L'a' && drive <= L'z');
}

inline bool isAllowedMediaVirtualHostPath(const HttpsUrlParts& parts) {
  if (parts.hasCredentials || parts.hasInvalidPort ||
      parts.hasNonDefaultPort || parts.hasSearchOrHash ||
      parts.path.size() <= 1U ||
      (parts.path.front() != L'/' && parts.path.front() != L'\\')) {
    return false;
  }
  const auto decoded = decodeUriPathSegment(parts.path.substr(1U));
  return decoded && *decoded != L"." && *decoded != L".." &&
         decoded->find_first_of(L"/\\") == std::wstring::npos;
}

inline std::optional<std::wstring> percentEncodeQueryComponent(
    std::wstring_view value) {
  try {
    if (value.empty()) return std::nullopt;
    std::string utf8;
    if (!appendWideAsUtf8(value, utf8)) return std::nullopt;
    constexpr wchar_t kHex[] = L"0123456789ABCDEF";
    std::wstring encoded;
    encoded.reserve(utf8.size() * 3U);
    for (const unsigned char byte : utf8) {
      const bool unreserved =
          (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
          (byte >= '0' && byte <= '9') || byte == '-' || byte == '.' ||
          byte == '_' || byte == '~';
      if (unreserved) {
        encoded.push_back(static_cast<wchar_t>(byte));
      } else {
        encoded.push_back(L'%');
        encoded.push_back(kHex[byte >> 4U]);
        encoded.push_back(kHex[byte & 0x0FU]);
      }
    }
    return encoded;
  } catch (...) {
    return std::nullopt;
  }
}

inline std::optional<std::wstring> escapeJsonString(std::wstring_view value) {
  try {
    std::wstring escaped;
    escaped.reserve(value.size() + 8U);
    constexpr wchar_t kHex[] = L"0123456789ABCDEF";
    for (const wchar_t character : value) {
      switch (character) {
        case L'"':
          escaped += L"\\\"";
          break;
        case L'\\':
          escaped += L"\\\\";
          break;
        case L'\b':
          escaped += L"\\b";
          break;
        case L'\f':
          escaped += L"\\f";
          break;
        case L'\n':
          escaped += L"\\n";
          break;
        case L'\r':
          escaped += L"\\r";
          break;
        case L'\t':
          escaped += L"\\t";
          break;
        default:
          if (character < 0x20) {
            const auto value16 = static_cast<unsigned int>(character);
            escaped += L"\\u00";
            escaped.push_back(kHex[(value16 >> 4U) & 0x0FU]);
            escaped.push_back(kHex[value16 & 0x0FU]);
          } else {
            escaped.push_back(character);
          }
          break;
      }
    }
    return escaped;
  } catch (...) {
    return std::nullopt;
  }
}

}  // namespace video_restore_detail

inline PersistedVideoSourceClass classifyPersistedVideoSource(
    std::wstring_view source) {
  if (source.empty() || source.find(L'\0') != std::wstring_view::npos ||
      source.size() > kWebView2MaxNavigationCodeUnits) {
    return PersistedVideoSourceClass::Rejected;
  }

  const auto navigationClass =
      WebView2Surface::classifyNavigation(source, false);
  if (navigationClass == WebView2Surface::NavigationClass::Denied ||
      navigationClass == WebView2Surface::NavigationClass::TestData) {
    return video_restore_detail::isDriveRootedLocalPath(source)
               ? PersistedVideoSourceClass::LocalFileCandidate
               : PersistedVideoSourceClass::Rejected;
  }

  const auto parts = video_restore_detail::splitHttpsUrl(source);
  if (!parts || parts->hasCredentials || parts->hasInvalidPort) {
    return PersistedVideoSourceClass::Rejected;
  }
  const auto normalizedHost =
      video_restore_detail::normalizeAsciiDnsHostname(parts->host);
  if (!normalizedHost) return PersistedVideoSourceClass::Rejected;

  constexpr std::wstring_view kCanvasHost = L"canvas.local";
  constexpr std::wstring_view kMediaHost = L"media.canvas.local";
  if (*normalizedHost == kCanvasHost) {
    const bool packagedAdapter =
        !parts->hasNonDefaultPort &&
        (parts->path == L"/video.html" ||
         parts->path == L"\\video.html");
    return packagedAdapter ? PersistedVideoSourceClass::PackagedAdapter
                           : PersistedVideoSourceClass::Rejected;
  }
  if (*normalizedHost == kMediaHost) {
    return video_restore_detail::isAllowedMediaVirtualHostPath(*parts)
               ? PersistedVideoSourceClass::UnrestorableVirtualHost
               : PersistedVideoSourceClass::Rejected;
  }
  if (video_restore_detail::asciiStartsWithIgnoreCase(
          *normalizedHost, L"canvas.local.") ||
      video_restore_detail::asciiStartsWithIgnoreCase(
          *normalizedHost, L"media.canvas.local.")) {
    return PersistedVideoSourceClass::Rejected;
  }
  return navigationClass == WebView2Surface::NavigationClass::Https
             ? PersistedVideoSourceClass::RemoteHttps
             : PersistedVideoSourceClass::Rejected;
}

inline std::optional<std::wstring> buildSetVideoSourceMessage(
    std::wstring_view nodeId, std::wstring_view source) {
  if (nodeId.empty() || nodeId.size() > 256U || source.empty() ||
      nodeId.find(L'\0') != std::wstring_view::npos ||
      source.find(L'\0') != std::wstring_view::npos) {
    return std::nullopt;
  }
  const auto escapedNodeId = video_restore_detail::escapeJsonString(nodeId);
  const auto escapedSource = video_restore_detail::escapeJsonString(source);
  if (!escapedNodeId || !escapedSource) return std::nullopt;
  try {
    std::wstring message =
        L"{\"protocolVersion\":1,\"type\":\"set-video-source\","
        L"\"nodeId\":\"";
    message += *escapedNodeId;
    message += L"\",\"payload\":{\"source\":\"";
    message += *escapedSource;
    message += L"\"}}";
    if (message.size() > kWebView2MaxMessageCodeUnits) return std::nullopt;
    return message;
  } catch (...) {
    return std::nullopt;
  }
}

inline std::optional<VideoRestorePlan> buildVideoRestorePlan(
    std::wstring_view nodeId, std::wstring_view persistedSource) {
  if (nodeId.empty() || nodeId.size() > 256U ||
      nodeId.find(L'\0') != std::wstring_view::npos) {
    return std::nullopt;
  }
  const auto encodedNodeId =
      video_restore_detail::percentEncodeQueryComponent(nodeId);
  if (!encodedNodeId) return std::nullopt;

  try {
    VideoRestorePlan plan;
    plan.sourceClass = classifyPersistedVideoSource(persistedSource);
    plan.navigationUri =
        L"https://canvas.local/video.html?nodeId=" + *encodedNodeId;
    switch (plan.sourceClass) {
      case PersistedVideoSourceClass::PackagedAdapter:
        plan.mediaAction = VideoRestoreMediaAction::None;
        break;
      case PersistedVideoSourceClass::RemoteHttps:
        plan.mediaAction = VideoRestoreMediaAction::UsePersistedRemote;
        plan.initialMessage =
            buildSetVideoSourceMessage(nodeId, persistedSource);
        if (!plan.initialMessage) return std::nullopt;
        break;
      case PersistedVideoSourceClass::LocalFileCandidate:
        // Deliberately keep handle-based approval in the application layer.
        // The pure plan only states that approval and a folder mapping are
        // required before a set-video-source message may be built.
        plan.mediaAction =
            VideoRestoreMediaAction::ApprovePersistedLocalFile;
        break;
      case PersistedVideoSourceClass::UnrestorableVirtualHost:
      case PersistedVideoSourceClass::Rejected:
        plan.mediaAction = VideoRestoreMediaAction::Reject;
        break;
    }
    return plan;
  } catch (...) {
    return std::nullopt;
  }
}

}  // namespace canvas::windows::detail
