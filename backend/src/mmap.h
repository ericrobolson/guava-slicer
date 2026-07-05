/// @file mmap.h
/// @brief Cross-platform read-only memory-mapped file I/O.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace mapped_file {

/// @brief Error codes for memory-mapped file operations.
enum class Error : uint8_t {
    NONE = 0,
    FILE_NOT_FOUND,
    STAT_FAILED,
    MMAP_FAILED,
    EMPTY_FILE,
};

/// @brief Read-only memory-mapped file handle.
struct MappedFile {
    const char* data = nullptr;
    size_t size = 0;
    Error error = Error::NONE;

#ifdef _WIN32
    HANDLE file_handle = INVALID_HANDLE_VALUE;
    HANDLE mapping_handle = nullptr;
#else
    int fd = -1;
#endif

    /// @brief Returns true if the file was mapped successfully.
    bool ok() const { return error == Error::NONE && data != nullptr; }
};

/// @brief Map a file into memory for reading.
inline MappedFile map_file(const char* path) {
    MappedFile result;

#ifdef _WIN32
    result.file_handle = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ,
                                     nullptr, OPEN_EXISTING,
                                     FILE_ATTRIBUTE_NORMAL, nullptr);
    if (result.file_handle == INVALID_HANDLE_VALUE) {
        result.error = Error::FILE_NOT_FOUND;
        return result;
    }

    LARGE_INTEGER file_size;
    if (!GetFileSizeEx(result.file_handle, &file_size)) {
        CloseHandle(result.file_handle);
        result.file_handle = INVALID_HANDLE_VALUE;
        result.error = Error::STAT_FAILED;
        return result;
    }
    result.size = static_cast<size_t>(file_size.QuadPart);

    if (result.size == 0) {
        CloseHandle(result.file_handle);
        result.file_handle = INVALID_HANDLE_VALUE;
        result.error = Error::EMPTY_FILE;
        return result;
    }

    result.mapping_handle = CreateFileMappingA(result.file_handle, nullptr,
                                               PAGE_READONLY, 0, 0, nullptr);
    if (!result.mapping_handle) {
        CloseHandle(result.file_handle);
        result.file_handle = INVALID_HANDLE_VALUE;
        result.error = Error::MMAP_FAILED;
        return result;
    }

    result.data = static_cast<const char*>(
        MapViewOfFile(result.mapping_handle, FILE_MAP_READ, 0, 0, 0));
    if (!result.data) {
        CloseHandle(result.mapping_handle);
        CloseHandle(result.file_handle);
        result.mapping_handle = nullptr;
        result.file_handle = INVALID_HANDLE_VALUE;
        result.error = Error::MMAP_FAILED;
        return result;
    }
#else
    result.fd = open(path, O_RDONLY);
    if (result.fd < 0) {
        result.error = Error::FILE_NOT_FOUND;
        return result;
    }

    struct stat st;
    if (fstat(result.fd, &st) != 0) {
        close(result.fd);
        result.fd = -1;
        result.error = Error::STAT_FAILED;
        return result;
    }
    result.size = static_cast<size_t>(st.st_size);

    if (result.size == 0) {
        close(result.fd);
        result.fd = -1;
        result.error = Error::EMPTY_FILE;
        return result;
    }

    void* ptr = ::mmap(nullptr, result.size, PROT_READ, MAP_PRIVATE, result.fd, 0);
    if (ptr == MAP_FAILED) {
        close(result.fd);
        result.fd = -1;
        result.error = Error::MMAP_FAILED;
        return result;
    }
    result.data = static_cast<const char*>(ptr);
#endif

    return result;
}

/// @brief Unmap a previously mapped file.
inline void unmap_file(MappedFile& file) {
#ifdef _WIN32
    if (file.data) {
        UnmapViewOfFile(file.data);
        file.data = nullptr;
    }
    if (file.mapping_handle) {
        CloseHandle(file.mapping_handle);
        file.mapping_handle = nullptr;
    }
    if (file.file_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(file.file_handle);
        file.file_handle = INVALID_HANDLE_VALUE;
    }
#else
    if (file.data && file.size > 0) {
        munmap(const_cast<char*>(file.data), file.size);
        file.data = nullptr;
    }
    if (file.fd >= 0) {
        close(file.fd);
        file.fd = -1;
    }
#endif
    file.size = 0;
    file.error = Error::NONE;
}

/// @brief Return a human-readable error string.
inline const char* error_string(Error err) {
    switch (err) {
        case Error::NONE:           return "no error";
        case Error::FILE_NOT_FOUND: return "file not found";
        case Error::STAT_FAILED:    return "failed to stat file";
        case Error::MMAP_FAILED:    return "failed to memory-map file";
        case Error::EMPTY_FILE:     return "file is empty";
    }
    return "unknown error";
}

} // namespace mapped_file
