// Fuzzes the client DBC (WDBC) loader exactly as DBCStorage<T>::Load(fn, nullptr)
// drives it at worldserver start-up: DBCFileLoader::Load, then AutoProduceData and
// AutoProduceStrings with one of the server's real record formats (DBCfmt.h).
// Input framing: byte 0 selects the format, the rest is the .dbc file.
#include "fuzz_common.h"
#include "DBCFileLoader.h"
#include "DBCfmt.h"
#include <G3D/System.h>

// g3dlite is linked into every harness; create its buffer pool up front as the server
// does (see fuzz_vmap_model.cpp), or its exit-time System::free(NULL) trips UBSan.
extern "C" int LLVMFuzzerInitialize(int*, char***)
{
    G3D::System::free(G3D::System::malloc(1));
    return 0;
}

static const char* const kFormats[] = {
    Achievementfmt, AreaTableEntryfmt, AreaTriggerEntryfmt, ChrClassesEntryfmt,
    ChrRacesEntryfmt, CreatureDisplayInfofmt, EmotesEntryfmt, FactionEntryfmt,
    GemPropertiesEntryfmt, ItemSetEntryfmt, LockEntryfmt, MapEntryfmt,
    SkillLinefmt, SpellEntryfmt, SpellItemEnchantmentfmt, TalentEntryfmt,
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < 1)
        return -1;
    const char* fmt = kFormats[data[0] % (sizeof(kFormats) / sizeof(kFormats[0]))];
    std::string path = fuzz_write_temp(data + 1, size - 1);

    // Mirrors DBCStorage<T>::Load (DBCStore.h) with no SQL side table.
    DBCFileLoader dbc;
    if (dbc.Load(path.c_str(), fmt))
    {
        uint32 count = 0;
        char** indexTable = nullptr;
        char* sqlDataTable = nullptr;
        char* dataTable = dbc.AutoProduceData(fmt, count, indexTable, 0, 0, sqlDataTable);
        char* strings = dbc.AutoProduceStrings(fmt, dataTable);
        delete[] strings;
        delete[] dataTable;
        delete[] indexTable;
    }
    unlink(path.c_str());
    return 0;
}
