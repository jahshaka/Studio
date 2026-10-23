#include "labclasses.h"

#include <algorithm>
#include <cmath>

namespace lab {

const std::vector<std::string> &stillnessClassNames()
{
    static const std::vector<std::string> n = { "plaster", "rough", "glossy", "mirror", "foliage", "mover" };
    return n;
}

unsigned char stillnessClassOf(const envs::Object &o)
{
    if (o.mover) return 6u;
    if (o.cutout) return 5u;   // foliage = an alpha-tested (cut-out) surface, the engine's class (PHOTON-2 K5)
    if (o.metallic >= 0.5f && o.roughness < 0.05f) return 4u;
    if (o.roughness < 0.4f) return 3u;
    if (o.roughness < 0.8f) return 2u;
    return 1u;
}

std::vector<unsigned char> stillnessClassImage(const std::vector<envs::Object> &objects, const iris::Vec3 &eye,
                                               const iris::Vec3 &target, float fovYDegrees, unsigned w, unsigned h)
{
    std::vector<unsigned char> out(size_t(w) * h, 0u);
    iris::Vec3 f = target - eye;
    if (!w || !h || f.length() < 1e-6f) return out;
    f = f.normalized();
    const iris::Vec3 right = iris::Vec3::crossProduct(f, iris::Vec3(0, 1, 0)).normalized();
    const iris::Vec3 up = iris::Vec3::crossProduct(right, f);
    const float tanY = std::tan(0.5f * fovYDegrees * float(M_PI) / 180.0f);
    const float aspect = float(w) / float(h);
    struct Box { float lo[3] = {}, hi[3] = {}; unsigned char cls = 0; };
    std::vector<Box> boxes;
    for (const envs::Object &o : objects)
        boxes.push_back({ { o.boxMin.x(), o.boxMin.y(), o.boxMin.z() }, { o.boxMax.x(), o.boxMax.y(), o.boxMax.z() },
                          stillnessClassOf(o) });
    const float org[3] = { eye.x(), eye.y(), eye.z() };
    for (unsigned y = 0; y < h; ++y)
        for (unsigned x = 0; x < w; ++x) {
            const float sx = (2.0f * (float(x) + 0.5f) / float(w) - 1.0f) * tanY * aspect;
            const float sy = (1.0f - 2.0f * (float(y) + 0.5f) / float(h)) * tanY;
            const iris::Vec3 d3 = f + right * sx + up * sy;
            const float d[3] = { d3.x(), d3.y(), d3.z() };
            float best = 1e30f;
            unsigned char bc = 0u;
            for (const Box &b : boxes) {
                float t0 = 0.0f, t1 = 1e30f;
                bool hit = true;
                for (int a = 0; a < 3 && hit; ++a) {
                    if (std::fabs(d[a]) < 1e-9f) {
                        if (org[a] < b.lo[a] || org[a] > b.hi[a]) hit = false;
                        continue;
                    }
                    float ta = (b.lo[a] - org[a]) / d[a], tb = (b.hi[a] - org[a]) / d[a];
                    if (ta > tb) std::swap(ta, tb);
                    t0 = std::max(t0, ta);
                    t1 = std::min(t1, tb);
                    if (t0 > t1) hit = false;
                }
                if (hit && t0 < best) { best = t0; bc = b.cls; }
            }
            out[size_t(y) * w + x] = bc;
        }
    return out;
}

}   // namespace lab
