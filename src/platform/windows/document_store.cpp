#include "platform/windows/document_store.h"

#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <string>
#include <string_view>

namespace canvas::windows {

namespace {

constexpr std::uint64_t kMaximumDocumentBytes = 512ULL * 1024ULL * 1024ULL;

std::string win32Error(DWORD code) {
  wchar_t* message = nullptr;
  const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER |
                      FORMAT_MESSAGE_FROM_SYSTEM |
                      FORMAT_MESSAGE_IGNORE_INSERTS;
  const DWORD length =
      FormatMessageW(flags, nullptr, code, 0,
                     reinterpret_cast<wchar_t*>(&message), 0, nullptr);
  if (length == 0 || message == nullptr) {
    return "Win32 error " + std::to_string(static_cast<unsigned long>(code));
  }
  int utf8Length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, message,
                                       static_cast<int>(length), nullptr, 0,
                                       nullptr, nullptr);
  if (utf8Length <= 0) {
    LocalFree(message);
    return "Win32 error " + std::to_string(static_cast<unsigned long>(code));
  }
  std::string result(static_cast<std::size_t>(utf8Length), '\0');
  WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, message,
                      static_cast<int>(length), result.data(), utf8Length,
                      nullptr, nullptr);
  LocalFree(message);
  while (!result.empty() && (result.back() == '\r' || result.back() == '\n' ||
                             result.back() == ' ' || result.back() == '\t')) {
    result.pop_back();
  }
  return result;
}

std::string errorFor(std::string_view operation, DWORD code) {
  std::string result(operation);
  result += ": ";
  result += win32Error(code);
  return result;
}

std::filesystem::path temporaryPath(const std::filesystem::path& path) {
  std::filesystem::path result = path;
  result += L".tmp";
  return result;
}

bool writeAll(HANDLE handle, const std::vector<std::uint8_t>& bytes,
              std::string& error) {
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const std::size_t remaining = bytes.size() - offset;
    const DWORD chunk = static_cast<DWORD>((std::min)(
        remaining,
        static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
    DWORD written = 0;
    if (!WriteFile(handle, bytes.data() + offset, chunk, &written, nullptr)) {
      error = errorFor("WriteFile", GetLastError());
      return false;
    }
    if (written == 0 || written > chunk) {
      error = "WriteFile returned an invalid byte count";
      return false;
    }
    offset += written;
  }
  return true;
}

}  // namespace

bool DocumentStore::saveAtomic(const std::filesystem::path& path,
                               const std::vector<std::uint8_t>& bytes,
                               std::string& error) {
  error.clear();
  if (path.empty()) {
    error = "saveAtomic: path is empty";
    return false;
  }
  const std::filesystem::path temp = temporaryPath(path);
  HANDLE handle = CreateFileW(
      temp.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    error = errorFor("CreateFileW", GetLastError());
    return false;
  }

  bool success = writeAll(handle, bytes, error);
  if (success && !FlushFileBuffers(handle)) {
    error = errorFor("FlushFileBuffers", GetLastError());
    success = false;
  }
  if (!CloseHandle(handle) && success) {
    error = errorFor("CloseHandle", GetLastError());
    success = false;
  }
  if (!success) {
    if (!DeleteFileW(temp.c_str())) {
      const DWORD cleanupError = GetLastError();
      if (cleanupError != ERROR_FILE_NOT_FOUND) {
        error += "; cleanup .tmp failed: ";
        error += win32Error(cleanupError);
      }
    }
    return false;
  }

  if (!MoveFileExW(temp.c_str(), path.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    error = errorFor("MoveFileExW", GetLastError());
    if (!DeleteFileW(temp.c_str())) {
      const DWORD cleanupError = GetLastError();
      if (cleanupError != ERROR_FILE_NOT_FOUND) {
        error += "; cleanup .tmp failed: ";
        error += win32Error(cleanupError);
      }
    }
    return false;
  }
  return true;
}

bool DocumentStore::load(const std::filesystem::path& path,
                         std::vector<std::uint8_t>& bytes, std::string& error) {
  error.clear();
  bytes.clear();
  if (path.empty()) {
    error = "load: path is empty";
    return false;
  }
  HANDLE handle =
      CreateFileW(path.c_str(), GENERIC_READ,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    error = errorFor("CreateFileW", GetLastError());
    return false;
  }

  LARGE_INTEGER fileSize{};
  if (!GetFileSizeEx(handle, &fileSize)) {
    error = errorFor("GetFileSizeEx", GetLastError());
    CloseHandle(handle);
    return false;
  }
  if (fileSize.QuadPart < 0 ||
      static_cast<std::uint64_t>(fileSize.QuadPart) > kMaximumDocumentBytes) {
    error = "document exceeds the 512 MiB load limit";
    CloseHandle(handle);
    return false;
  }
  const std::size_t size = static_cast<std::size_t>(fileSize.QuadPart);
  try {
    bytes.resize(size);
  } catch (const std::exception& exception) {
    error =
        std::string("unable to allocate document buffer: ") + exception.what();
    CloseHandle(handle);
    bytes.clear();
    return false;
  }

  std::size_t offset = 0;
  bool success = true;
  while (offset < bytes.size()) {
    const std::size_t remaining = bytes.size() - offset;
    const DWORD chunk = static_cast<DWORD>((std::min)(
        remaining,
        static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
    DWORD read = 0;
    if (!ReadFile(handle, bytes.data() + offset, chunk, &read, nullptr)) {
      error = errorFor("ReadFile", GetLastError());
      success = false;
      break;
    }
    if (read == 0 || read > chunk) {
      error = "ReadFile returned an invalid byte count";
      success = false;
      break;
    }
    offset += read;
  }
  if (!CloseHandle(handle) && success) {
    error = errorFor("CloseHandle", GetLastError());
    success = false;
  }
  if (!success) bytes.clear();
  return success;
}

}  // namespace canvas::windows
