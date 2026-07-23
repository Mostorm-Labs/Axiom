#pragma once

#include <windows.h>

#include <cwctype>
#include <string>
#include <string_view>
#include <vector>

namespace canvas::windows::detail {

inline bool isPathSeparator(wchar_t value) noexcept {
  return value == L'\\' || value == L'/';
}

// A fully-qualified Windows path is either drive-rooted or UNC/device rooted.
// Drive-relative forms such as C:assets intentionally do not qualify.
inline bool isFullyQualifiedWindowsPath(std::wstring_view path) noexcept {
  if (path.size() >= 3 && std::iswalpha(path[0]) != 0 && path[1] == L':' &&
      isPathSeparator(path[2])) {
    return true;
  }
  return path.size() >= 2 && isPathSeparator(path[0]) &&
         isPathSeparator(path[1]);
}

inline HRESULT win32FailureOr(HRESULT fallback) noexcept {
  const DWORD error = GetLastError();
  return error == ERROR_SUCCESS ? fallback : HRESULT_FROM_WIN32(error);
}

inline HRESULT normalizeVirtualHostFolder(
    std::wstring_view folder, std::wstring_view executableDirectory,
    std::wstring& normalized) {
  normalized.clear();
  if (folder.empty() || folder.find(L'\0') != std::wstring_view::npos ||
      executableDirectory.find(L'\0') != std::wstring_view::npos) {
    return E_INVALIDARG;
  }

  std::wstring candidate;
  if (isFullyQualifiedWindowsPath(folder)) {
    candidate.assign(folder);
  } else {
    if (!isFullyQualifiedWindowsPath(executableDirectory)) {
      return E_INVALIDARG;
    }
    // Reject drive-relative input rather than letting GetFullPathName consult
    // the process's per-drive current directory.
    if (folder.size() >= 2 && folder[1] == L':') return E_INVALIDARG;
    // A single leading slash is drive-root-relative and therefore ambiguous.
    if (isPathSeparator(folder.front())) return E_INVALIDARG;
    candidate.assign(executableDirectory);
    const bool baseHasSeparator =
        !candidate.empty() && isPathSeparator(candidate.back());
    if (!baseHasSeparator) candidate.push_back(L'\\');
    candidate.append(folder);
  }

  SetLastError(ERROR_SUCCESS);
  DWORD capacity = GetFullPathNameW(candidate.c_str(), 0, nullptr, nullptr);
  if (capacity == 0) return win32FailureOr(E_INVALIDARG);

  std::vector<wchar_t> buffer(capacity);
  for (;;) {
    SetLastError(ERROR_SUCCESS);
    const DWORD written = GetFullPathNameW(
        candidate.c_str(), static_cast<DWORD>(buffer.size()), buffer.data(),
        nullptr);
    if (written == 0) return win32FailureOr(E_INVALIDARG);
    if (written < buffer.size()) {
      normalized.assign(buffer.data(), written);
      break;
    }
    buffer.resize(static_cast<std::size_t>(written) + 1U);
  }

  if (!isFullyQualifiedWindowsPath(normalized)) return E_INVALIDARG;
  // WebView2 1.0.4078.44 documents folderPath as MAX_PATH including the NUL.
  if (normalized.size() >= MAX_PATH) {
    normalized.clear();
    return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
  }
  return S_OK;
}

inline HRESULT directoryFromExecutablePath(std::wstring_view modulePath,
                                           std::wstring& directory) {
  directory.clear();
  if (modulePath.empty() ||
      modulePath.find(L'\0') != std::wstring_view::npos) {
    return E_INVALIDARG;
  }
  const std::size_t separator = modulePath.find_last_of(L"\\/");
  if (separator == std::wstring_view::npos) return E_UNEXPECTED;
  const bool driveRoot = separator == 2 && modulePath.size() >= 3 &&
                         modulePath[1] == L':' &&
                         isPathSeparator(modulePath[2]);
  directory.assign(modulePath.substr(0, separator + (driveRoot ? 1U : 0U)));
  return isFullyQualifiedWindowsPath(directory) ? S_OK : E_UNEXPECTED;
}

inline HRESULT executableDirectory(std::wstring& directory) {
  directory.clear();
  std::vector<wchar_t> buffer(260);
  for (;;) {
    SetLastError(ERROR_SUCCESS);
    const DWORD copied = GetModuleFileNameW(
        nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (copied == 0) return win32FailureOr(E_FAIL);
    if (copied < buffer.size()) {
      std::wstring modulePath(buffer.data(), copied);
      return directoryFromExecutablePath(modulePath, directory);
    }
    if (buffer.size() > 32768U) {
      return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
    }
    buffer.resize(buffer.size() * 2U);
  }
}

}  // namespace canvas::windows::detail
