// Shared helper: the upstream loaders take a file name (fopen/fread), so each
// input is written to a private temp file under $TMPDIR and removed afterwards.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

static std::string fuzz_write_temp(const uint8_t* data, size_t size)
{
    const char* dir = getenv("TMPDIR");
    std::string path = std::string(dir && *dir ? dir : "/tmp") + "/skyfire_fuzz_XXXXXX";
    int fd = mkstemp(&path[0]);
    if (fd < 0)
        abort();
    size_t off = 0;
    while (off < size)
    {
        ssize_t n = write(fd, data + off, size - off);
        if (n <= 0)
            abort();
        off += size_t(n);
    }
    close(fd);
    return path;
}
