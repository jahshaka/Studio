/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "export/walkers/meshbufferreader.h"

#include <QList>

#include <algorithm>
#include <cmath>

#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/assets/vertexbuffer.h"

namespace exportwalk {

bool extractMeshBuffers(iris::Mesh *mesh, MeshBuffers &out)
{
    if (!mesh) return false;
    for (const auto &vb : mesh->getVertexBuffers()) {
        if (!vb || !vb->data) continue;
        const QList<iris::VertexAttribute> attribs = vb->vertexLayout.getAttribs();
        if (attribs.isEmpty()) continue;
        const iris::VertexAttribute &attr = attribs.first();
        const float *f = reinterpret_cast<const float *>(vb->data);
        const int floats = vb->dataSize / int(sizeof(float));
        switch (attr.usage) {
        case iris::VertexAttribUsage::Position:
            out.positions.assign(f, f + floats); break;
        case iris::VertexAttribUsage::Normal:
            out.normals.assign(f, f + floats); break;
        case iris::VertexAttribUsage::TexCoord0: {
            // assimp stores texcoords as 3 floats; exporters want 2 (mirror does the same)
            const int comps = attr.count > 0 ? attr.count : 3;
            for (int i = 0; i + comps <= floats; i += comps) {
                out.uvs.push_back(f[i]); out.uvs.push_back(f[i + 1]);
            }
            break;
        }
        case iris::VertexAttribUsage::BoneIndices:
            out.boneIndices.assign(f, f + floats); break;
        case iris::VertexAttribUsage::BoneWeights:
            out.boneWeights.assign(f, f + floats); break;
        default: break;
        }
    }
    if (out.positions.empty()) return false;
    const size_t nv = out.vertexCount();
    const iris::IndexBufferPtr ib = mesh->getIndexBuffer();
    if (ib && ib->data && ib->dataSize > 0) {
        const unsigned *idx = reinterpret_cast<const unsigned *>(ib->data);
        out.indices.assign(idx, idx + ib->dataSize / int(sizeof(unsigned)));
    } else {
        out.indices.resize(nv);
        for (size_t i = 0; i < nv; ++i) out.indices[i] = unsigned(i);
    }
    if (out.normals.size() != nv * 3) out.normals.clear();
    if (out.uvs.size() != nv * 2) out.uvs.clear();
    if (out.boneIndices.size() != nv * 4 || out.boneWeights.size() != nv * 4) {
        out.boneIndices.clear(); out.boneWeights.clear();
    }
    return out.indices.size() >= 3;
}

void generateNormals(MeshBuffers &m)
{
    const size_t nv = m.vertexCount(), ni = m.indices.size();
    m.normals.assign(nv * 3, 0.0f);
    for (size_t t = 0; t + 2 < ni; t += 3) {
        const unsigned a = m.indices[t], b = m.indices[t + 1], cc = m.indices[t + 2];
        if (a >= nv || b >= nv || cc >= nv) continue;
        const float *pa = &m.positions[a * 3], *pb = &m.positions[b * 3], *pc = &m.positions[cc * 3];
        const float e1[3] = { pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2] };
        const float e2[3] = { pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2] };
        const float n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                             e1[0] * e2[1] - e1[1] * e2[0] };
        for (unsigned v : { a, b, cc })
            for (int k = 0; k < 3; ++k) m.normals[v * 3 + k] += n[k];
    }
    for (size_t v = 0; v < nv; ++v) {
        float *n = &m.normals[v * 3];
        const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (len > 1e-8f) { n[0] /= len; n[1] /= len; n[2] /= len; } else { n[1] = 1.0f; }
    }
}

void generateTangents(MeshBuffers &m)
{
    const size_t nv = m.vertexCount(), ni = m.indices.size();
    m.tangents.assign(nv * 4, 0.0f);
    std::vector<float> bitan(nv * 3, 0.0f);
    if (!m.uvs.empty()) {
        for (size_t t = 0; t + 2 < ni; t += 3) {
            const unsigned a = m.indices[t], b = m.indices[t + 1], cc = m.indices[t + 2];
            if (a >= nv || b >= nv || cc >= nv) continue;
            const float *pa = &m.positions[a * 3], *pb = &m.positions[b * 3], *pc = &m.positions[cc * 3];
            const float *ua = &m.uvs[a * 2], *ub = &m.uvs[b * 2], *uc = &m.uvs[cc * 2];
            const float e1[3] = { pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2] };
            const float e2[3] = { pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2] };
            const float s1 = ub[0] - ua[0], t1 = ub[1] - ua[1];
            const float s2 = uc[0] - ua[0], t2 = uc[1] - ua[1];
            const float det = s1 * t2 - s2 * t1;
            if (std::fabs(det) < 1e-12f) continue;
            const float r = 1.0f / det;
            const float T[3] = { (t2 * e1[0] - t1 * e2[0]) * r, (t2 * e1[1] - t1 * e2[1]) * r,
                                 (t2 * e1[2] - t1 * e2[2]) * r };
            const float B[3] = { (s1 * e2[0] - s2 * e1[0]) * r, (s1 * e2[1] - s2 * e1[1]) * r,
                                 (s1 * e2[2] - s2 * e1[2]) * r };
            for (unsigned v : { a, b, cc })
                for (int k = 0; k < 3; ++k) { m.tangents[v * 4 + k] += T[k]; bitan[v * 3 + k] += B[k]; }
        }
    }
    for (size_t v = 0; v < nv; ++v) {
        const float *n = &m.normals[v * 3];
        float *t = &m.tangents[v * 4];
        const float ndt = n[0] * t[0] + n[1] * t[1] + n[2] * t[2];
        float tx = t[0] - n[0] * ndt, ty = t[1] - n[1] * ndt, tz = t[2] - n[2] * ndt;
        const float len = std::sqrt(tx * tx + ty * ty + tz * tz);
        if (len > 1e-8f) { tx /= len; ty /= len; tz /= len; }
        else {
            if (std::fabs(n[0]) < 0.9f) { tx = 1.0f - n[0] * n[0]; ty = -n[0] * n[1]; tz = -n[0] * n[2]; }
            else                        { tx = -n[1] * n[0]; ty = 1.0f - n[1] * n[1]; tz = -n[1] * n[2]; }
            const float l2 = std::sqrt(tx * tx + ty * ty + tz * tz);
            tx /= l2; ty /= l2; tz /= l2;
        }
        const float cx = n[1] * tz - n[2] * ty, cy = n[2] * tx - n[0] * tz, cz = n[0] * ty - n[1] * tx;
        const float *b = &bitan[v * 3];
        t[0] = tx; t[1] = ty; t[2] = tz;
        t[3] = (cx * b[0] + cy * b[1] + cz * b[2]) < 0.0f ? -1.0f : 1.0f;
    }
}

} // namespace exportwalk
