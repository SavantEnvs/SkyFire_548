// Fuzzes the client DB2 (WDB2) loader exactly as DB2Storage<T>::Load(fn, locale)
// drives it (DB2Store.h): DB2FileLoader::Load, AutoProduceData,
// AutoProduceStringsArrayHolders and AutoProduceStrings, with one of the
// server's real record formats (DB2fmt.h).
// Input framing: byte 0 selects the format, the rest is the .db2 file.
#include "fuzz_common.h"
#include "DB2FileLoader.h"
#include "DB2fmt.h"
#include <G3D/System.h>

// g3dlite is linked into every harness; create its buffer pool up front as the server
// does (see fuzz_vmap_model.cpp), or its exit-time System::free(NULL) trips UBSan.
extern "C" int LLVMFuzzerInitialize(int*, char***)
{
    G3D::System::free(G3D::System::malloc(1));
    return 0;
}

static const char* const kFormats[] = {
    BattlePetAbilityEffectfmt, BattlePetAbilityfmt, BattlePetBreedQualityfmt,
    BattlePetEffectPropertiesfmt, BattlePetSpeciesfmt, BroadcastTextfmt,
    ItemCurrencyCostfmt, ItemExtendedCostEntryfmt, Itemfmt, ItemSparsefmt,
    KeyChainfmt, SceneScriptPackagefmt, SpellReagentsfmt, BattlePetVisualfmt,
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < 1)
        return -1;
    const char* fmt = kFormats[data[0] % (sizeof(kFormats) / sizeof(kFormats[0]))];
    std::string path = fuzz_write_temp(data + 1, size - 1);

    DB2FileLoader db2;
    if (db2.Load(path.c_str(), fmt))
    {
        uint32 count = 0;
        char** indexTable = nullptr;
        char* dataTable = db2.AutoProduceData(fmt, count, indexTable);
        char* holders = db2.AutoProduceStringsArrayHolders(fmt, dataTable);
        char* strings = db2.AutoProduceStrings(fmt, dataTable, 0 /* LOCALE_enUS */);
        delete[] strings;
        delete[] holders;
        delete[] dataTable;
        delete[] indexTable;
    }
    unlink(path.c_str());
    return 0;
}
