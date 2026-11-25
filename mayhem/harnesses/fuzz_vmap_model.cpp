// Fuzzes the VMAP world-model (.vmo) reader the worldserver uses for line of
// sight and height queries: VMAP::WorldModel::readFile (GroupModel, WmoLiquid,
// MeshTriangle and BIH chunks), then the queries the server runs on a loaded model.
#include "fuzz_common.h"
#include "WorldModel.h"
#include "MapTree.h"
#include "ModelInstance.h"
#include <G3D/System.h>

// g3dlite's buffer pool is created lazily by the first System::malloc; in the
// worldserver that happens long before any .vmo is read. Without it, the
// System::free(NULL) of a never-filled static Array at process exit calls a member
// function through a null BufferPool (UBSan) on every run, unrelated to the input.
extern "C" int LLVMFuzzerInitialize(int*, char***)
{
    G3D::System::free(G3D::System::malloc(1));
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    std::string path = fuzz_write_temp(data, size);
    {
        VMAP::WorldModel model;
        if (model.readFile(path))
        {
            G3D::Vector3 p(1.0f, 2.0f, 3.0f), down(0.0f, 0.0f, -1.0f);
            G3D::Ray ray = G3D::Ray::fromOriginAndDirection(G3D::Vector3(-100.0f, -100.0f, 50.0f),
                                                            G3D::Vector3(1.0f, 1.0f, -0.5f).direction());
            float dist = 1000.0f;
            model.IntersectRay(ray, dist, false);
            VMAP::AreaInfo area;
            dist = 1000.0f;
            model.IntersectPoint(p, down, dist, area);
            VMAP::LocationInfo loc;
            dist = 1000.0f;
            model.GetLocationInfo(p, down, dist, loc);
        }
    }
    unlink(path.c_str());
    return 0;
}
