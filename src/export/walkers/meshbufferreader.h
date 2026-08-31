/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef EXPORT_MESHBUFFERREADER_H
#define EXPORT_MESHBUFFERREADER_H

// Mesh geometry readers for exporters (ASSET_PIPELINE_SPEC §3.3): the document
// mesh's vertex/index buffers as plain float/uint arrays, plus the smooth-
// normal and Lengyel-tangent generators the engine mirror runs (irisgl/engine/
// src/OgreMesh.cpp buildMeshV2), ported so exported shading matches the
// viewport. Extracted from the glTF writer; pure document consumers.

#include <vector>

namespace iris {
class Mesh;
}

namespace exportwalk {

struct MeshBuffers
{
    std::vector<float> positions, normals, uvs, tangents;
    std::vector<float> boneIndices, boneWeights;   // 4 per vertex, float (document layout)
    std::vector<unsigned> indices;
    size_t vertexCount() const { return positions.size() / 3; }
};

/// Reads positions/normals/uvs/skin weights and indices straight from
/// iris::Mesh::getVertexBuffers(). UVs are narrowed from the document's
/// 3-float texcoords to 2 (the mirror does the same); missing indices become
/// a trivial 0..n-1 list. Returns false when there is no usable geometry
/// (no positions, or fewer than 3 indices). Attribute arrays whose sizes
/// don't match the vertex count are dropped, not exported wrong.
bool extractMeshBuffers(iris::Mesh *mesh, MeshBuffers &out);

/// Area-weighted smooth normals over the index list (overwrites m.normals).
void generateNormals(MeshBuffers &m);

/// Float4 Lengyel tangents (w = handedness from the bitangent), orthonormalised
/// against the normals; degenerate UVs fall back to an arbitrary stable frame.
/// Requires normals (generate first when absent).
void generateTangents(MeshBuffers &m);

} // namespace exportwalk

#endif // EXPORT_MESHBUFFERREADER_H
