// Known-answer tests for the file loaders the fuzz targets exercise (built with
// the project's normal flags by mayhem/build.sh, run by mayhem/test.sh).
// Each test builds a file in a known layout, loads it with the upstream loader
// and checks the decoded values. Prints "PASS <name>" / "FAIL <name>" lines.
#include "../harnesses/fuzz_common.h"
#include "DBCFileLoader.h"
#include "DB2FileLoader.h"
#include "WorldModel.h"
#include "MapTree.h"
#include "ModelInstance.h"
#include "Config.h"
#include <cmath>
#include <cstring>
#include <vector>

static int g_failed = 0, g_passed = 0;
static void report(const char* name, bool ok)
{
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    (ok ? g_passed : g_failed)++;
}

static void put32(std::vector<uint8_t>& v, uint32 x)
{
    for (int i = 0; i < 4; ++i)
        v.push_back(uint8_t(x >> (8 * i)));
}
static void putf(std::vector<uint8_t>& v, float f)
{
    uint32 x;
    memcpy(&x, &f, 4);
    put32(v, x);
}

// WDBC, 2 records of fmt "nsfi": id, string offset, float, int
static void test_dbc()
{
    std::vector<uint8_t> f;
    const char strings[] = "\0alpha\0beta";
    put32(f, 0x43424457); put32(f, 2); put32(f, 4); put32(f, 16); put32(f, sizeof(strings));
    put32(f, 7);  put32(f, 1); putf(f, 1.5f);  put32(f, 100);
    put32(f, 3);  put32(f, 7); putf(f, -2.0f); put32(f, 200);
    f.insert(f.end(), strings, strings + sizeof(strings));
    std::string p = fuzz_write_temp(f.data(), f.size());

#pragma pack(push, 1)  // as DBCStructure.h / DB2Structure.h
    struct Rec { uint32 id; char* name; float val; uint32 num; };
#pragma pack(pop)
    const char* fmt = "nsfi";
    DBCFileLoader dbc;
    bool ok = dbc.Load(p.c_str(), fmt);
    report("dbc_load", ok);
    report("dbc_header", ok && dbc.GetNumRows() == 2 && dbc.GetCols() == 4);
    if (ok)
    {
        uint32 count = 0; char** index = nullptr; char* sql = nullptr;
        char* table = dbc.AutoProduceData(fmt, count, index, 0, 0, sql);
        char* pool = dbc.AutoProduceStrings(fmt, table);
        report("dbc_index_size", count == 8);
        Rec* r7 = reinterpret_cast<Rec*>(index[7]);
        Rec* r3 = reinterpret_cast<Rec*>(index[3]);
        report("dbc_index_lookup", r7 && r3 && !index[0] && !index[5]);
        report("dbc_values", r7 && r3 && r7->id == 7 && r7->val == 1.5f && r7->num == 100 &&
                             r3->id == 3 && r3->val == -2.0f && r3->num == 200);
        report("dbc_strings", r7 && r3 && r7->name && r3->name &&
                              !strcmp(r7->name, "alpha") && !strcmp(r3->name, "beta"));
        delete[] pool; delete[] table; delete[] index;
    }
    std::vector<uint8_t> bad(f);
    bad[0] = 'X';
    std::string pb = fuzz_write_temp(bad.data(), bad.size());
    DBCFileLoader dbc2;
    report("dbc_bad_magic_rejected", !dbc2.Load(pb.c_str(), fmt));
    unlink(p.c_str()); unlink(pb.c_str());
}

// WDB2 (no index table: maxIndex = 0), 1 record of fmt "nis"
static void test_db2()
{
    std::vector<uint8_t> f;
    const char strings[] = "\0gamma";
    put32(f, 0x32424457); put32(f, 1); put32(f, 3); put32(f, 12); put32(f, sizeof(strings));
    // build <= 12880: no min/max index, locale or copy-table words follow
    put32(f, 0xABCD1234 /*hash*/); put32(f, 1 /*build*/); put32(f, 0 /*timestamp*/);
    put32(f, 5); put32(f, 42); put32(f, 1);
    f.insert(f.end(), strings, strings + sizeof(strings));
    std::string p = fuzz_write_temp(f.data(), f.size());

    const char* fmt = "nis";
    DB2FileLoader db2;
    bool ok = db2.Load(p.c_str(), fmt);
    report("db2_load", ok);
    report("db2_header", ok && db2.GetNumRows() == 1 && db2.GetCols() == 3 && db2.GetHash() == 0xABCD1234);
    if (ok)
    {
        uint32 count = 0; char** index = nullptr;
        char* table = db2.AutoProduceData(fmt, count, index);
        char* holders = db2.AutoProduceStringsArrayHolders(fmt, table);
        char* pool = db2.AutoProduceStrings(fmt, table, 0);
        report("db2_index_size", count == 6 && index && index[5] && !index[0]);
        uint32* rec = reinterpret_cast<uint32*>(index[5]);
        report("db2_values", rec && rec[0] == 5 && rec[1] == 42);
        char const** ls = nullptr;
        if (rec)
            memcpy(&ls, reinterpret_cast<char*>(rec) + 8, sizeof(ls));
        report("db2_strings", ls && ls[0] && !strcmp(ls[0], "gamma"));
        delete[] pool; delete[] holders; delete[] table; delete[] index;
    }
    unlink(p.c_str());
}

// Build a one-triangle world model, write it with writeFile, read it back and
// check ray/point queries against hand-computed answers.
static void test_vmap()
{
    using namespace G3D;
    std::vector<Vector3> verts = { Vector3(0, 0, 0), Vector3(10, 0, 0), Vector3(0, 10, 0) };
    std::vector<VMAP::MeshTriangle> tris = { VMAP::MeshTriangle(0, 1, 2) };
    std::vector<VMAP::GroupModel> groups;
    groups.push_back(VMAP::GroupModel(0x8, 77, AABox(Vector3(0, 0, -1), Vector3(10, 10, 1))));
    groups[0].setMeshData(verts, tris);
    VMAP::WorldModel out;
    out.setRootWmoID(1234);
    out.setGroupModels(groups);

    const char* dir = getenv("TMPDIR");
    std::string p = std::string(dir && *dir ? dir : "/tmp") + "/skyfire_selftest.vmo";
    report("vmap_write", out.writeFile(p));

    VMAP::WorldModel in;
    bool ok = in.readFile(p);
    report("vmap_read", ok);
    VMAP::LocationInfo loc;
    float zdist = 100.0f;
    bool inside = in.GetLocationInfo(Vector3(2, 2, 0.5f), Vector3(0, 0, -1), zdist, loc);
    report("vmap_location", inside && loc.hitModel && loc.hitModel->GetWmoID() == 77 &&
                            loc.hitModel->GetMogpFlags() == 0x8 && std::fabs(zdist - 0.5f) < 1e-3f);

    float dist = 100.0f;
    Ray down = Ray::fromOriginAndDirection(Vector3(2, 2, 5), Vector3(0, 0, -1));
    bool hit = in.IntersectRay(down, dist, false);
    report("vmap_ray_hit", hit && std::fabs(dist - 5.0f) < 1e-3f);

    dist = 100.0f;
    Ray miss = Ray::fromOriginAndDirection(Vector3(9, 9, 5), Vector3(0, 0, -1));
    report("vmap_ray_miss", !in.IntersectRay(miss, dist, false));

    std::vector<uint8_t> trunc = { 'V', 'M', 'A', 'P' };
    std::string pt = fuzz_write_temp(trunc.data(), trunc.size());
    VMAP::WorldModel bad;
    report("vmap_truncated_rejected", !bad.readFile(pt));
    unlink(p.c_str()); unlink(pt.c_str());
}

static void test_config()
{
    const char text[] =
        "[worldserver]\n"
        "# a comment line\n"
        "  DataDir = \"./data\" ; trailing comment\n"
        "WorldServerPort=8085\n"
        "vmap.enableLOS = yes\n"
        "Rate.Health = 1.5\n"
        "Appender.Console = 1,3,0\n"
        "[extra]\n"
        "Motd = 'hash # kept'\n"
        "noequals\n";
    std::string p = fuzz_write_temp(reinterpret_cast<const uint8_t*>(text), sizeof(text) - 1);
    bool loaded = sConfigMgr->LoadInitial(p.c_str());
    report("config_loaded", loaded);
    report("config_quoted_string", sConfigMgr->GetStringDefault("DataDir", "") == "./data");
    report("config_int_bool_float", sConfigMgr->GetIntDefault("WorldServerPort", 0) == 8085 &&
                                    sConfigMgr->GetBoolDefault("vmap.enableLOS", false) &&
                                    std::fabs(sConfigMgr->GetFloatDefault("Rate.Health", 0.0f) - 1.5f) < 1e-6f);
    report("config_quoted_comment_char", sConfigMgr->GetStringDefault("Motd", "") == "hash # kept" &&
                                         sConfigMgr->GetStringDefault("noequals", "dflt") == "dflt");
    std::list<std::string> keys = sConfigMgr->GetKeysByString("Appender.");
    report("config_keys_by_string", keys.size() == 1 && keys.front() == "Appender.Console");
    unlink(p.c_str());
}

int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    test_dbc();
    test_db2();
    test_vmap();
    test_config();
    printf("SUMMARY passed=%d failed=%d\n", g_passed, g_failed);
    return g_failed ? 1 : 0;
}
