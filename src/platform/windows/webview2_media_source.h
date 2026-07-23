#pragma once

#include "platform/windows/webview2_virtual_host_path.h"

#include <windows.h>

#include <cstddef>
#include <cwctype>
#include <string>
#include <string_view>
#include <vector>

namespace canvas::windows::detail {

struct LocalMediaSource {
  std::wstring folder;
  std::wstring uri;
};

inline HRESULT dosPathFromFinalHandlePath(std::wstring_view finalPath,
                                          std::wstring& dosPath) {
  dosPath.clear();
  constexpr std::wstring_view kDrivePrefix = L"\\\\?\\";
  constexpr std::wstring_view kUncPrefix = L"\\\\?\\UNC\\";
  if (finalPath.rfind(kUncPrefix, 0) == 0) {
    dosPath.assign(L"\\\\");
    dosPath.append(finalPath.substr(kUncPrefix.size()));
  } else if (finalPath.rfind(kDrivePrefix, 0) == 0) {
    dosPath.assign(finalPath.substr(kDrivePrefix.size()));
  } else {
    return E_INVALIDARG;
  }
  return isFullyQualifiedWindowsPath(dosPath) ? S_OK : E_INVALIDARG;
}

inline bool isDriveRoot(std::wstring_view path) noexcept {
  while (path.size() > 3 && isPathSeparator(path.back())) {
    path.remove_suffix(1);
  }
  return path.size() == 3 && std::iswalpha(path[0]) != 0 &&
         path[1] == L':' && isPathSeparator(path[2]);
}

inline bool isUncShareRoot(std::wstring_view path) noexcept {
  while (path.size() > 2 && isPathSeparator(path.back())) {
    path.remove_suffix(1);
  }
  if (path.size() < 5 || !isPathSeparator(path[0]) ||
      !isPathSeparator(path[1])) {
    return false;
  }
  const std::size_t serverEnd = path.find_first_of(L"\\/", 2);
  if (serverEnd == 2) return false;
  if (serverEnd == std::wstring_view::npos) return true;
  const std::size_t shareEnd = path.find_first_of(L"\\/", serverEnd + 1);
  return shareEnd == std::wstring_view::npos && serverEnd + 1 < path.size();
}

inline bool isUnsafeVirtualHostRoot(std::wstring_view path) noexcept {
  return isDriveRoot(path) || isUncShareRoot(path);
}

inline HRESULT percentEncodeUtf8PathSegment(std::wstring_view segment,
                                            std::wstring& encoded) {
  encoded.clear();
  if (segment.empty() ||
      segment.find(L'\0') != std::wstring_view::npos) {
    return E_INVALIDARG;
  }
  const int sourceLength = static_cast<int>(segment.size());
  if (sourceLength < 0 || static_cast<std::size_t>(sourceLength) !=
                              segment.size()) {
    return E_INVALIDARG;
  }
  const int utf8Length = WideCharToMultiByte(
      CP_UTF8, WC_ERR_INVALID_CHARS, segment.data(), sourceLength, nullptr, 0,
      nullptr, nullptr);
  if (utf8Length <= 0) return win32FailureOr(E_INVALIDARG);
  std::string utf8(static_cast<std::size_t>(utf8Length), '\0');
  if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, segment.data(),
                          sourceLength, utf8.data(), utf8Length, nullptr,
                          nullptr) != utf8Length) {
    return win32FailureOr(E_INVALIDARG);
  }

  constexpr wchar_t kHex[] = L"0123456789ABCDEF";
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
  return S_OK;
}

inline HRESULT deriveLocalMediaSource(std::wstring_view absoluteFilePath,
                                      LocalMediaSource& source) {
  source = {};
  if (!isFullyQualifiedWindowsPath(absoluteFilePath) ||
      absoluteFilePath.find(L'\0') != std::wstring_view::npos ||
      absoluteFilePath.rfind(L"\\\\?\\", 0) == 0 ||
      absoluteFilePath.rfind(L"\\\\.\\", 0) == 0) {
    return E_INVALIDARG;
  }

  const std::size_t separator = absoluteFilePath.find_last_of(L"\\/");
  if (separator == std::wstring_view::npos ||
      separator + 1U >= absoluteFilePath.size()) {
    return E_INVALIDARG;
  }
  const std::wstring_view filename = absoluteFilePath.substr(separator + 1U);
  if (filename == L"." || filename == L".." ||
      filename.find_first_of(L"\\/:*?\"<>|") != std::wstring_view::npos) {
    return E_INVALIDARG;
  }
  const std::size_t extensionStart = filename.find_last_of(L'.');
  if (extensionStart == std::wstring_view::npos) {
    return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
  }
  std::wstring extension(filename.substr(extensionStart));
  for (wchar_t& value : extension) {
    value = static_cast<wchar_t>(std::towlower(value));
  }
  if (extension != L".mp4" && extension != L".webm" &&
      extension != L".m4v" && extension != L".mov") {
    return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
  }

  std::wstring folder;
  const bool driveRootParent = separator == 2 && absoluteFilePath.size() >= 3;
  folder.assign(absoluteFilePath.substr(
      0, separator + (driveRootParent ? 1U : 0U)));
  if (folder.empty() || isUnsafeVirtualHostRoot(folder)) {
    return E_ACCESSDENIED;
  }

  std::wstring encodedFilename;
  const HRESULT encodeResult =
      percentEncodeUtf8PathSegment(filename, encodedFilename);
  if (FAILED(encodeResult)) return encodeResult;
  source.folder = std::move(folder);
  source.uri = L"https://media.canvas.local/" + encodedFilename;
  return S_OK;
}

inline HRESULT approveLocalMediaFile(std::wstring_view filePath,
                                     LocalMediaSource& source) {
  source = {};
  if (!isFullyQualifiedWindowsPath(filePath) || filePath.empty() ||
      filePath.find(L'\0') != std::wstring_view::npos) {
    return E_INVALIDARG;
  }

  const std::wstring candidate(filePath);
  SetLastError(ERROR_SUCCESS);
  const HANDLE file = CreateFileW(
      candidate.c_str(), FILE_READ_ATTRIBUTES,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return win32FailureOr(HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));
  }

  HRESULT result = S_OK;
  BY_HANDLE_FILE_INFORMATION information{};
  if (!GetFileInformationByHandle(file, &information)) {
    result = win32FailureOr(E_FAIL);
  } else if ((information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
    result = HRESULT_FROM_WIN32(ERROR_DIRECTORY);
  } else if (GetFileType(file) != FILE_TYPE_DISK) {
    result = HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
  }

  std::wstring resolved;
  if (SUCCEEDED(result)) {
    SetLastError(ERROR_SUCCESS);
    const DWORD capacity = GetFinalPathNameByHandleW(
        file, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (capacity == 0) {
      result = win32FailureOr(E_FAIL);
    } else {
      std::vector<wchar_t> buffer(static_cast<std::size_t>(capacity) + 1U);
      SetLastError(ERROR_SUCCESS);
      const DWORD written = GetFinalPathNameByHandleW(
          file, buffer.data(), static_cast<DWORD>(buffer.size()),
          FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
      if (written == 0 || written >= buffer.size()) {
        result = win32FailureOr(E_FAIL);
      } else {
        result = dosPathFromFinalHandlePath(
            std::wstring_view(buffer.data(), written), resolved);
      }
    }
  }
  CloseHandle(file);
  return FAILED(result) ? result : deriveLocalMediaSource(resolved, source);
}

}  // namespace canvas::windows::detail
