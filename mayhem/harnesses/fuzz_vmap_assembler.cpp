// Fuzzes the vmap4_assembler converter, VMAP::TileAssembler::convertWorld2 (the same
// call vmap4_assembler's main makes): readMapSpawns (dir_bin / ModelSpawn records),
// calculateTransformedBound, exportGameobjectModels (temp_gameobject_models), and
// WorldModel_Raw / GroupModel_Raw::Read on the raw model files, then the
// .vmtree/.vmtile/.vmo writers.
//
// Input framing (all integers little-endian), carved into a private temp source dir:
//   u32 n1 | n1 bytes -> <src>/dir_bin
//   u32 n2 | n2 bytes -> <src>/m          (raw model; spawns and object lists name it "m")
//   rest              -> <src>/temp_gameobject_models
// The output goes to <tmp>/out; the whole temp tree is removed after each input.
#include "fuzz_common.h"
#include "TileAssembler.h"
#include <G3D/System.h>
#include <cstring>
#include <filesystem>

static bool write_file(const std::string& path, const uint8_t* data, size_t size)
{
    FILE* f = fopen(path.c_str(), "wb");
    if (!f)
        return false;
    bool ok = size == 0 || fwrite(data, 1, size, f) == size;
    fclose(f);
    return ok;
}

static bool take(const uint8_t*& data, size_t& size, const uint8_t*& part, size_t& len)
{
    if (size < 4)
        return false;
    uint32_t n;
    memcpy(&n, data, 4);
    data += 4;
    size -= 4;
    if (n > size)
        n = uint32_t(size);
    part = data;
    len = n;
    data += n;
    size -= n;
    return true;
}

extern "C" int LLVMFuzzerInitialize(int*, char***)
{
    // See fuzz_vmap_model.cpp: create g3dlite's buffer pool the way the tool does.
    G3D::System::free(G3D::System::malloc(1));
    // The assembler prints progress for every spawn and model; keep the fuzzer quiet.
    if (!freopen("/dev/null", "w", stdout))
        return 0;
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    const uint8_t *dir_bin, *model;
    size_t dir_len, model_len;
    if (!take(data, size, dir_bin, dir_len) || !take(data, size, model, model_len))
        return -1;

    const char* tmp = getenv("TMPDIR");
    std::string root = std::string(tmp && *tmp ? tmp : "/tmp") + "/skyfire_vmapasm_XXXXXX";
    if (!mkdtemp(&root[0]))
        abort();
    std::string src = root + "/src", dst = root + "/out";
    std::filesystem::create_directory(src);
    std::filesystem::create_directory(dst);

    if (write_file(src + "/dir_bin", dir_bin, dir_len) && write_file(src + "/m", model, model_len)
        && write_file(src + "/temp_gameobject_models", data, size))
    {
        VMAP::TileAssembler ta(src, dst);
        ta.convertWorld2();
    }

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    return 0;
}
