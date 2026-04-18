// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Freeminer team

#pragma once

#include "content_mapblock.h"
#include "client/meshgen/collector.h"
#include "mapblock_mesh.h"

// PolyVox includes
#include <PolyVox/RawVolume.h>
#include <PolyVox/CubicSurfaceExtractor.h>
#include <PolyVox/MarchingCubesSurfaceExtractor.h>
#include <PolyVox/Mesh.h>
#include <PolyVox/Material.h>
#include <array>
#include <unordered_map>

class PolyVoxMesher
{
public:
    PolyVoxMesher(MeshMakeData* data, MeshCollector* collector);
    ~PolyVoxMesher() = default;

    void generate();

private:
    MeshMakeData* m_data;
    MeshCollector* m_collector;
    const NodeDefManager *nodedef;
    const v3pos_t blockpos_nodes;
    std::unordered_map<content_t, uint8_t> m_content_to_material;
    std::array<content_t, 256> m_material_to_content{};
    uint8_t m_next_material_id = 1;

    // current node (matching MapblockMeshGenerator structure)
    struct {
        v3pos_t pf;
        v3pos_t pr;

        v3pos_t p; // relative to blockpos_nodes
        v3f origin; // p in BS space
        MapNode n;
        const ContentFeatures *f;
        LightFrame lframe; // smooth lighting
        video::SColor lcolor; // unsmooth lighting
    } cur_node;

    // Convert Freeminer nodes to PolyVox volume
    void fillVolume(PolyVox::RawVolume<PolyVox::Material8>& volume);
    
    // Extract mesh from PolyVox volume
    void extractMesh(PolyVox::RawVolume<PolyVox::Material8>& volume);
    
    // Convert PolyVox mesh to Freeminer mesh format
    void convertToCollector(const PolyVox::Mesh<PolyVox::CubicVertex<PolyVox::Material8>>& polyvoxMesh);
    void convertToCollector(const PolyVox::Mesh<PolyVox::Vertex<PolyVox::Material8>>& polyvoxMesh);
    void convertToCollector(const PolyVox::Mesh<PolyVox::Vertex<uint8_t>>& polyvoxMesh);
    v3pos_t resolveTriangleNodePos(const v3f &base_pos0, const v3f &base_pos1,
        const v3f &base_pos2, const v3f &normal) const;
    v3pos_t remapVolumePosToNodePos(const v3pos_t &volume_pos) const;
    void resetMaterialMappings();
    void setupNodeFromMaterial(const PolyVox::Material8 &material, const v3pos_t &fallback_pos);
    
    // Helper functions
    PolyVox::Material8 nodeToMaterial(const MapNode& node);
    MapNode materialToNode(const PolyVox::Material8& material);
    
    // Lighting functions (copied from MapblockMeshGenerator)
    void getSmoothLightFrame();
    LightInfo blendLight(const v3f &vertex_pos);
    video::SColor blendLightColor(const v3f &vertex_pos);
    video::SColor blendLightColor(const v3f &vertex_pos, const v3f &vertex_normal);
};
