// Fuzzes the server's .conf parser (authserver.conf / worldserver.conf and the
// LoadMore files scripts add): ConfigMgr::LoadInitial -> LoadData (section headers,
// comments, quoting, key=value), then reads values back the way the servers do.
#include "fuzz_common.h"
#include "Config.h"
#include <G3D/System.h>

// g3dlite is linked into every harness; create its buffer pool up front as the server
// does (see fuzz_vmap_model.cpp), or its exit-time System::free(NULL) trips UBSan.
extern "C" int LLVMFuzzerInitialize(int*, char***)
{
    G3D::System::free(G3D::System::malloc(1));
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    std::string path = fuzz_write_temp(data, size);
    if (sConfigMgr->LoadInitial(path.c_str()))
    {
        sConfigMgr->GetStringDefault("LoginDatabaseInfo", "");
        sConfigMgr->GetStringDefault("DataDir", "./");
        sConfigMgr->GetIntDefault("RealmServerPort", 3724);
        sConfigMgr->GetIntDefault("WorldServerPort", 8085);
        sConfigMgr->GetBoolDefault("vmap.enableLOS", true);
        sConfigMgr->GetFloatDefault("Rate.Health", 1.0f);
        for (std::string const& key : sConfigMgr->GetKeysByString("Appender."))
            sConfigMgr->GetStringDefault(key.c_str(), "");
        sConfigMgr->GetKeysByString("");
        sConfigMgr->Reload();
        sConfigMgr->LoadMore(path.c_str());
    }
    unlink(path.c_str());
    return 0;
}
