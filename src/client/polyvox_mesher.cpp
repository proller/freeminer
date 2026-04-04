// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Freeminer team

#include "polyvox_mesher.h"
#include "util/numeric.h"
#include "nodedef.h"
#include "settings.h"
#include "constants.h"
#include "PolyVox/RawVolume.h"
#include "PolyVox/CubicSurfaceExtractor.h"
#include "PolyVox/MarchingCubesSurfaceExtractor.h"
#include "PolyVox/VolumeResampler.h"
#include "PolyVox/Mesh.h"
#include "PolyVox/Material.h"
#include "PolyVox/DefaultMarchingCubesController.h"
#include "client/tile.h"
#include "client/mesh.h"
#include "client/tile.h"
#include "util/directiontables.h"
#include "util/numeric.h"

using namespace PolyVox;

// Maps light index to corner direction (copied from content_mapblock.cpp)
static const v3pos_t light_dirs[8] = {
	v3pos_t(-1, -1, -1),
	v3pos_t(-1, -1,  1),
	v3pos_t(-1,  1, -1),
	v3pos_t(-1,  1,  1),
	v3pos_t( 1, -1, -1),
	v3pos_t( 1, -1,  1),
	v3pos_t( 1,  1, -1),
	v3pos_t( 1,  1,  1),
};

// Standard index set to make a quad on 4 vertices
static constexpr u16 quad_indices_02[] = {0, 1, 2, 2, 3, 0};
static constexpr u16 quad_indices_13[] = {0, 1, 3, 3, 1, 2};
static const auto &quad_indices = quad_indices_02;

PolyVoxMesher::PolyVoxMesher(MeshMakeData* data, MeshCollector* collector)
    : m_data(data)
    , m_collector(collector)
    , nodedef(data->m_nodedef)
    , blockpos_nodes(data->m_blockpos * MAP_BLOCKSIZE)
{
}

void PolyVoxMesher::generate()
{
    // Create a PolyVox volume for the entire mesh chunk
    const u16 chunk_size = m_data->m_mesh_grid.cell_size * MAP_BLOCKSIZE;
    Region region(Vector3DInt32(0, 0, 0), 
                  Vector3DInt32(chunk_size - 1, chunk_size - 1, chunk_size - 1));
    RawVolume<Material8> volume(region);
    
    // Fill the volume with node data
    fillVolume(volume);
    
    // Extract mesh from the volume
    extractMesh(volume);
}

void PolyVoxMesher::fillVolume(RawVolume<Material8>& volume)
{
    v3pos_t blockpos_nodes = m_data->m_blockpos * MAP_BLOCKSIZE;
    
    // Use the same iteration pattern as MapblockMeshGenerator::generate()
    const auto lstep = 1 << m_data->lod_step;
    const auto fstep = 1 << m_data->far_step;
    
    // Iterate over the entire mesh chunk (multiple map blocks)
    const u16 chunk_size_nodes = m_data->m_mesh_grid.cell_size * MAP_BLOCKSIZE;
    
    for (cur_node.pf.Z = cur_node.pr.Z = 0; cur_node.pr.Z < chunk_size_nodes; cur_node.pr.Z+=lstep, cur_node.pf.Z+=fstep)
        for (cur_node.pf.X = cur_node.pr.X = 0; cur_node.pr.X < chunk_size_nodes; cur_node.pr.X += lstep, cur_node.pf.X += fstep) {
            for (cur_node.pf.Y = cur_node.pr.Y = 0; cur_node.pr.Y < chunk_size_nodes; cur_node.pr.Y += lstep, cur_node.pf.Y += fstep) {
                // Use the appropriate position based on far_step
                cur_node.p = (m_data->far_step ? cur_node.pf : cur_node.pr);
                MapNode node = m_data->m_vmanip.getNodeNoEx(blockpos_nodes + cur_node.p);
                Material8 material = nodeToMaterial(node);
                // PolyVox coordinates are 0-based and relative to the volume
                if (cur_node.pr.X < chunk_size_nodes && cur_node.pr.Y < chunk_size_nodes && cur_node.pr.Z < chunk_size_nodes) {
                    volume.setVoxel(cur_node.pr.X, cur_node.pr.Y, cur_node.pr.Z, material);
                }
            }
        }
}

void PolyVoxMesher::extractMesh(RawVolume<Material8>& volume)
{
    // Check if we should use smooth mesh based on settings or distance
    bool useSmoothMesh = (m_data->lod_step > 0) || (m_data->fscale > 1);
//useSmoothMesh = 1;
    if (useSmoothMesh) {
        // Create temporary volume with uint8_t data type that marching cubes can handle
        Region region = volume.getEnclosingRegion();
        RawVolume<uint8_t> tempVolume(region);
        
        // Convert Material8 volume to uint8_t volume
        for (int32_t z = region.getLowerZ(); z <= region.getUpperZ(); z++) {
            for (int32_t y = region.getLowerY(); y <= region.getUpperY(); y++) {
                for (int32_t x = region.getLowerX(); x <= region.getUpperX(); x++) {
                    Material8 material = volume.getVoxel(x, y, z);
                    tempVolume.setVoxel(x, y, z, material.getMaterial());
                }
            }
        }
        
        // Extract smooth mesh using marching cubes on the volume (already at correct LOD)
        // Use default controller for uint8_t data
        DefaultMarchingCubesController<uint8_t> controller;
        auto mesh = extractMarchingCubesMesh(&tempVolume, region, controller);
        printf("PolyVoxMesher: Marching cubes mesh - %d vertices, %d indices\n", 
               (int)mesh.getNoOfVertices(), (int)mesh.getNoOfIndices());
        
        // Skip processing if mesh is too small (likely noise or floating pieces)
        const uint32_t minIndices = 12; // Minimum 4 triangles
        if (mesh.getNoOfIndices() < minIndices) {
            printf("PolyVoxMesher: Skipping tiny mesh with %d indices (below threshold %d)\n",
                   (int)mesh.getNoOfIndices(), (int)minIndices);
        } else {
            // Debug: Check if we have data in the volume
            if (mesh.getNoOfVertices() == 0) {
                printf("PolyVoxMesher: Empty mesh detected, checking volume data...\n");
                int nonZeroCount = 0;
                uint8_t maxVal = 0;
                for (int32_t z = region.getLowerZ(); z <= region.getUpperZ() && nonZeroCount < 10; z++) {
                    for (int32_t y = region.getLowerY(); y <= region.getUpperY() && nonZeroCount < 10; y++) {
                        for (int32_t x = region.getLowerX(); x <= region.getUpperX() && nonZeroCount < 10; x++) {
                            uint8_t val = tempVolume.getVoxel(x, y, z);
                            if (val > maxVal) maxVal = val;
                            if (val > 0) {
                                printf("PolyVoxMesher: Non-zero voxel at (%d,%d,%d) = %d\n", x, y, z, val);
                                nonZeroCount++;
                            }
                        }
                    }
                }
                if (nonZeroCount == 0) {
                    printf("PolyVoxMesher: No non-zero voxels found in volume! Max value: %d\n", maxVal);
                } else {
                    printf("PolyVoxMesher: Found %d non-zero voxels, max value: %d\n", nonZeroCount, maxVal);
                }
            }
            auto decodedMesh = decodeMesh(mesh);
            
            // Convert the extracted mesh to our collector format
            convertToCollector(decodedMesh);
        }
    } else {
        // Extract the surface mesh using PolyVox cubic surface extractor (original behavior)
        auto mesh = extractCubicMesh(&volume, volume.getEnclosingRegion());
        auto decodedMesh = decodeMesh(mesh);
        
        // Convert the extracted mesh to our collector format
        convertToCollector(decodedMesh);
    }
}

void PolyVoxMesher::convertToCollector(const Mesh<CubicVertex<Material8>>& polyvoxMesh)
{
    if (polyvoxMesh.getNoOfIndices() == 0 || polyvoxMesh.getNoOfVertices() == 0)
        return;
    
    // Calculate the offset that the original system expects
    // This matches the offset calculation in mapblock_mesh.cpp
    v3f offset = oposToV3f(intToFloat((m_data->m_blockpos - m_data->m_mesh_grid.getMeshPos(m_data->m_blockpos)) * MAP_BLOCKSIZE, BS));
    
    // Process triangles from the mesh
    const uint32_t numTriangles = polyvoxMesh.getNoOfIndices() / 3;
    
    // Debug output
    printf("PolyVoxMesher: Processing mesh with %d vertices and %d indices (%d triangles)\n", 
           (int)polyvoxMesh.getNoOfVertices(), (int)polyvoxMesh.getNoOfIndices(), (int)numTriangles);
    
    for (uint32_t triIdx = 0; triIdx < numTriangles; triIdx++) {
        
        // Get the three vertices of this triangle
        uint32_t idx0 = polyvoxMesh.getIndex(triIdx * 3 + 0);
        uint32_t idx1 = polyvoxMesh.getIndex(triIdx * 3 + 1);
        uint32_t idx2 = polyvoxMesh.getIndex(triIdx * 3 + 2);
        
        if (idx0 >= polyvoxMesh.getNoOfVertices() || 
            idx1 >= polyvoxMesh.getNoOfVertices() || 
            idx2 >= polyvoxMesh.getNoOfVertices()) {
            continue;
        }
        
        auto vertex0 = polyvoxMesh.getVertex(idx0);
        auto vertex1 = polyvoxMesh.getVertex(idx1);
        auto vertex2 = polyvoxMesh.getVertex(idx2);
        
        // For CubicVertex, decode the position
        // Convert to world coordinates and create vertices
        // The volume is positioned relative to the mesh grid origin
        Vector3DFloat decodedPos0 = decodePosition(vertex0.encodedPosition);
        Vector3DFloat decodedPos1 = decodePosition(vertex1.encodedPosition);
        Vector3DFloat decodedPos2 = decodePosition(vertex2.encodedPosition);
        
        // Calculate vertex positions relative to mesh grid origin
        v3f base_pos0 = oposToV3f(v3opos_t(decodedPos0.getX(), decodedPos0.getY(), decodedPos0.getZ()) * BS);
        v3f base_pos1 = oposToV3f(v3opos_t(decodedPos1.getX(), decodedPos1.getY(), decodedPos1.getZ()) * BS);
        v3f base_pos2 = oposToV3f(v3opos_t(decodedPos2.getX(), decodedPos2.getY(), decodedPos2.getZ()) * BS);
        
        v3f pos0, pos1, pos2;
        
        // Apply fscale scaling if needed (matching content_mapblock.cpp logic)
        if (m_data->fscale > 1) {
            int fscale = m_data->fscale;
            
            // Apply the same scaling transformation as in content_mapblock.cpp
            // First, shift to node center
            pos0 = base_pos0 + v3f(HBS, 0.0f, HBS);
            // Scale uniformly
            pos0 *= v3f((float)fscale, (float)fscale, (float)fscale);
            // Apply offset: -HBS for X/Z, and special Y offset calculation
            pos0 += v3f(-HBS, -HBS * (float)fscale + HBS + BS, -HBS);
            
            pos1 = base_pos1 + v3f(HBS, 0.0f, HBS);
            pos1 *= v3f((float)fscale, (float)fscale, (float)fscale);
            pos1 += v3f(-HBS, -HBS * (float)fscale + HBS + BS, -HBS);
            
            pos2 = base_pos2 + v3f(HBS, 0.0f, HBS);
            pos2 *= v3f((float)fscale, (float)fscale, (float)fscale);
            pos2 += v3f(-HBS, -HBS * (float)fscale + HBS + BS, -HBS);
        } else {
            pos0 = base_pos0;
            pos1 = base_pos1;
            pos2 = base_pos2;
        }
        
        // Apply the offset that the collector expects
        pos0 += offset;
        pos1 += offset;
        pos2 += offset;
        
        // Debug output for first few triangles
        if (triIdx < 5) {
            printf("PolyVoxMesher: Triangle %d: (%.2f,%.2f,%.2f) (%.2f,%.2f,%.2f) (%.2f,%.2f,%.2f)\n",
                   (int)triIdx,
                   pos0.X, pos0.Y, pos0.Z,
                   pos1.X, pos1.Y, pos1.Z,
                   pos2.X, pos2.Y, pos2.Z);
        }
        
        // Calculate normal (cross product of two edges)
        v3f edge1 = pos1 - pos0;
        v3f edge2 = pos2 - pos0;
        v3f normal = edge1.crossProduct(edge2);
        if (normal.getLengthSQ() > 0.0f) {
            normal.normalize();
        } else {
            normal = v3f(0, 1, 0);
        }
        
        // Debug: Print normal for first few triangles
        if (triIdx < 5) {
            printf("PolyVoxMesher: Tri %d: normal=(%.3f,%.3f,%.3f)\n",
                   (int)triIdx, normal.X, normal.Y, normal.Z);
        }
        
        // Determine which node this triangle belongs to by looking at the vertex positions
        // Since PolyVox generates faces at voxel boundaries, we need to map back to the solid voxel
        // The normal tells us which direction the face is pointing, so we can find the solid voxel
        v3pos_t base_node_pos = v3pos_t(
            (int)std::floor(base_pos0.X / BS + 0.5f),
            (int)std::floor(base_pos0.Y / BS + 0.5f),
            (int)std::floor(base_pos0.Z / BS + 0.5f)
        );
        
        // Adjust position based on normal to get the solid voxel
        // If normal points in positive direction, solid voxel is in negative direction
        v3pos_t triangle_node_pos = base_node_pos;
        const float normal_threshold = 0.1f;
        
        if (normal.X > normal_threshold) {
            triangle_node_pos.X -= 1; // Face points +X, solid is to -X
        } else if (normal.X < -normal_threshold) {
            triangle_node_pos.X += 1; // Face points -X, solid is to +X
        } else if (normal.Y > normal_threshold) {
            triangle_node_pos.Y -= 1; // Face points +Y, solid is to -Y
        } else if (normal.Y < -normal_threshold) {
            triangle_node_pos.Y += 1; // Face points -Y, solid is to +Y
        } else if (normal.Z > normal_threshold) {
            triangle_node_pos.Z -= 1; // Face points +Z, solid is to -Z
        } else if (normal.Z < -normal_threshold) {
            triangle_node_pos.Z += 1; // Face points -Z, solid is to +Z
        }
        // If normal is near zero or ambiguous, use the base position
        
        // Set up current node for this triangle
        cur_node.p = triangle_node_pos;
        cur_node.origin = oposToV3f(intToFloat(cur_node.p, BS));
        cur_node.n = m_data->m_vmanip.getNodeNoEx(blockpos_nodes + cur_node.p);
        cur_node.f = &nodedef->get(cur_node.n);
        
        // Debug: Print node info for first few triangles
        if (triIdx < 5) {
            printf("PolyVoxMesher: Tri %d: base=(%.1f,%.1f,%.1f) node=(%d,%d,%d) content=%d material=%d\n",
                   (int)triIdx, base_pos0.X, base_pos0.Y, base_pos0.Z,
                   triangle_node_pos.X, triangle_node_pos.Y, triangle_node_pos.Z,
                   (int)cur_node.n.getContent(), (int)nodeToMaterial(cur_node.n).getMaterial());
        }
        
        // Debug: Print lighting info
        if (triIdx < 3) {
            printf("PolyVoxMesher: Tri %d: smooth_lighting=%d content=%d\n",
                   (int)triIdx, m_data->m_smooth_lighting ? 1 : 0, (int)cur_node.n.getContent());
        }
        
        // Get lighting for this node
        if (m_data->m_smooth_lighting) {
            getSmoothLightFrame();
            cur_node.lcolor = blendLightColor(v3f(0, 0, 0)); // center of node
            if (triIdx < 3) {
                printf("PolyVoxMesher: Tri %d: smooth light color=(%d,%d,%d,%d)\n",
                       (int)triIdx,
                       cur_node.lcolor.getAlpha(), cur_node.lcolor.getRed(), 
                       cur_node.lcolor.getGreen(), cur_node.lcolor.getBlue());
            }
        } else {
            auto light = LightPair(getInteriorLight(cur_node.n, 0, nodedef));
            cur_node.lcolor = encode_light(light, cur_node.f->light_source);
            if (triIdx < 3) {
                printf("PolyVoxMesher: Tri %d: standard light=(%d,%d) color=(%d,%d,%d,%d)\n",
                       (int)triIdx, light.lightDay, light.lightNight,
                       cur_node.lcolor.getAlpha(), cur_node.lcolor.getRed(), 
                       cur_node.lcolor.getGreen(), cur_node.lcolor.getBlue());
            }
        }
        
        // Determine face direction based on normal for proper tile selection
        v3pos_t face_dir(0, 1, 0); // default to top face
        const float threshold = 0.5f;
        
        if (std::abs(normal.Y) > threshold && std::abs(normal.Y) > std::abs(normal.X) && std::abs(normal.Y) > std::abs(normal.Z)) {
            face_dir = v3pos_t(0, normal.Y > 0 ? 1 : -1, 0);
        } else if (std::abs(normal.X) > threshold && std::abs(normal.X) > std::abs(normal.Z)) {
            face_dir = v3pos_t(normal.X > 0 ? 1 : -1, 0, 0);
        } else if (std::abs(normal.Z) > threshold) {
            face_dir = v3pos_t(0, 0, normal.Z > 0 ? 1 : -1);
        }
        
        // Get the appropriate tile for this face direction
        TileSpec tile;
        
        // Use directional tile selection for all nodes (this ensures proper texture mapping)
        getNodeTile(cur_node.n, cur_node.p, face_dir, m_data, tile);
        
        // Apply backface culling for solid nodes (matching content_mapblock.cpp behavior)
        bool backface_culling = cur_node.f->drawtype == NDT_NORMAL || m_data->fscale > 1;
        if (backface_culling) {
            for (auto &layer : tile.layers) {
                if (layer.texture) {
                    layer.material_flags |= MATERIAL_FLAG_BACKFACE_CULLING;
                }
            }
        }
        
        // Apply tile animations and transformations (ported from content_mapblock.cpp)
        for (auto &layer : tile.layers) {
            if (layer.texture) {
                // Apply animation transformations
                if (layer.material_flags & MATERIAL_FLAG_ANIMATION) {
                    // Handle texture animation
                    // This is a simplified version - in reality you'd want to implement
                    // proper animation frame calculation like in the original
                }
                
                // Apply texture transforms
                if (layer.scale != 1) {
                    // Handle scaled textures
                }
                
                // Debug: Check for missing textures
                if (triIdx < 3 && !layer.texture) {
                    printf("PolyVoxMesher: Tri %d: WARNING - Missing texture for layer!\n", (int)triIdx);
                }
            }
        }
        
        // Debug: Print tile info for first few triangles
        if (triIdx < 3) {
            printf("PolyVoxMesher: Tri %d: face_dir=(%d,%d,%d) has_texture=%s scale=%d drawtype=%d\n",
                   (int)triIdx, face_dir.X, face_dir.Y, face_dir.Z,
                   tile.layers[0].texture ? "yes" : "no",
                   tile.layers[0].texture ? tile.layers[0].scale : 0,
                   (int)cur_node.f->drawtype);
        }
        
        // Generate texture coordinates based on the triangle vertices
        // Improved approach - generate UVs relative to the face and node position
        v2f uv0, uv1, uv2;
        
        // Calculate UV coordinates relative to the node's face
        // Use the vertex positions within the node's coordinate system
        v3f node_origin = cur_node.origin; // Center of the current node
        
        // Calculate relative positions within the node (normalized to [-0.5, 0.5])
        v3f rel_pos0 = (pos0 - node_origin) / BS;
        v3f rel_pos1 = (pos1 - node_origin) / BS;
        v3f rel_pos2 = (pos2 - node_origin) / BS;
        
        // Generate UV coordinates based on face normal orientation
        if (std::abs(normal.Y) > 0.5f) {
            // Top/bottom face - map X,Z coordinates
            uv0 = v2f(rel_pos0.X + 0.5f, rel_pos0.Z + 0.5f);
            uv1 = v2f(rel_pos1.X + 0.5f, rel_pos1.Z + 0.5f);
            uv2 = v2f(rel_pos2.X + 0.5f, rel_pos2.Z + 0.5f);
        } else if (std::abs(normal.X) > 0.5f) {
            // Side face X-aligned - map Z,Y coordinates
            uv0 = v2f(rel_pos0.Z + 0.5f, rel_pos0.Y + 0.5f);
            uv1 = v2f(rel_pos1.Z + 0.5f, rel_pos1.Y + 0.5f);
            uv2 = v2f(rel_pos2.Z + 0.5f, rel_pos2.Y + 0.5f);
        } else {
            // Side face Z-aligned - map X,Y coordinates
            uv0 = v2f(rel_pos0.X + 0.5f, rel_pos0.Y + 0.5f);
            uv1 = v2f(rel_pos1.X + 0.5f, rel_pos1.Y + 0.5f);
            uv2 = v2f(rel_pos2.X + 0.5f, rel_pos2.Y + 0.5f);
        }
        
        // Apply tile-specific transformations from content_mapblock.cpp approach
        // Handle texture scaling and other tile properties
        for (auto &layer : tile.layers) {
            if (layer.texture) {
                // Apply texture scaling
                if (layer.scale != 1) {
                    uv0.X *= layer.scale;
                    uv0.Y *= layer.scale;
                    uv1.X *= layer.scale;
                    uv1.Y *= layer.scale;
                    uv2.X *= layer.scale;
                    uv2.Y *= layer.scale;
                }
            }
        }
        
        // Debug: Print UV coordinates for first few triangles
        if (triIdx < 3) {
            printf("PolyVoxMesher: Tri %d: UVs=(%.3f,%.3f) (%.3f,%.3f) (%.3f,%.3f)\n",
                   (int)triIdx, uv0.X, uv0.Y, uv1.X, uv1.Y, uv2.X, uv2.Y);
        }
        
        // Create vertices with proper lighting and texture coordinates
        video::S3DVertex vertices[3];
        if (m_data->m_smooth_lighting) {
            vertices[0] = video::S3DVertex(pos0, normal, blendLightColor(pos0, normal), uv0);
            vertices[1] = video::S3DVertex(pos1, normal, blendLightColor(pos1, normal), uv1);
            vertices[2] = video::S3DVertex(pos2, normal, blendLightColor(pos2, normal), uv2);
        } else {
            video::SColor color = cur_node.lcolor;
            if (!cur_node.f->light_source) {
                applyFacesShading(color, normal);
            }
            vertices[0] = video::S3DVertex(pos0, normal, color, uv0);
            vertices[1] = video::S3DVertex(pos1, normal, color, uv1);
            vertices[2] = video::S3DVertex(pos2, normal, color, uv2);
        }
        
        // Debug: Print vertex colors for first few triangles
        if (triIdx < 3) {
            printf("PolyVoxMesher: Tri %d: colors=(%d,%d,%d,%d) (%d,%d,%d,%d) (%d,%d,%d,%d)\n",
                   (int)triIdx,
                   vertices[0].Color.getAlpha(), vertices[0].Color.getRed(), vertices[0].Color.getGreen(), vertices[0].Color.getBlue(),
                   vertices[1].Color.getAlpha(), vertices[1].Color.getRed(), vertices[1].Color.getGreen(), vertices[1].Color.getBlue(),
                   vertices[2].Color.getAlpha(), vertices[2].Color.getRed(), vertices[2].Color.getGreen(), vertices[2].Color.getBlue());
        }
        
        // Add triangle to collector
        u16 indices[3] = {0, 1, 2};
        m_collector->append(tile, vertices, 3, indices, 3);
    }
    
    printf("PolyVoxMesher: Finished processing %d triangles\n", (int)numTriangles);
}

void PolyVoxMesher::convertToCollector(const Mesh<Vertex<Material8>>& polyvoxMesh)
{
    if (polyvoxMesh.getNoOfIndices() == 0 || polyvoxMesh.getNoOfVertices() == 0)
        return;
    
    // Calculate the offset that the original system expects
    // This matches the offset calculation in mapblock_mesh.cpp
    v3f offset = oposToV3f(intToFloat((m_data->m_blockpos - m_data->m_mesh_grid.getMeshPos(m_data->m_blockpos)) * MAP_BLOCKSIZE, BS));
    
    // Process triangles from the mesh
    const uint32_t numTriangles = polyvoxMesh.getNoOfIndices() / 3;
    
    // Debug output
    printf("PolyVoxMesher (smooth): Processing mesh with %d vertices and %d indices (%d triangles)\n", 
           (int)polyvoxMesh.getNoOfVertices(), (int)polyvoxMesh.getNoOfIndices(), (int)numTriangles);
    
    for (uint32_t triIdx = 0; triIdx < numTriangles; triIdx++) {
        
        // Get the three vertices of this triangle
        uint32_t idx0 = polyvoxMesh.getIndex(triIdx * 3 + 0);
        uint32_t idx1 = polyvoxMesh.getIndex(triIdx * 3 + 1);
        uint32_t idx2 = polyvoxMesh.getIndex(triIdx * 3 + 2);
        
        if (idx0 >= polyvoxMesh.getNoOfVertices() || 
            idx1 >= polyvoxMesh.getNoOfVertices() || 
            idx2 >= polyvoxMesh.getNoOfVertices()) {
            continue;
        }
        
        auto vertex0 = polyvoxMesh.getVertex(idx0);
        auto vertex1 = polyvoxMesh.getVertex(idx1);
        auto vertex2 = polyvoxMesh.getVertex(idx2);
        
        // For decoded Vertex, position is already available
        Vector3DFloat decodedPos0 = vertex0.position;
        Vector3DFloat decodedPos1 = vertex1.position;
        Vector3DFloat decodedPos2 = vertex2.position;
        
        // Calculate vertex positions relative to mesh grid origin
        v3f base_pos0 = oposToV3f(v3opos_t(decodedPos0.getX(), decodedPos0.getY(), decodedPos0.getZ()) * BS);
        v3f base_pos1 = oposToV3f(v3opos_t(decodedPos1.getX(), decodedPos1.getY(), decodedPos1.getZ()) * BS);
        v3f base_pos2 = oposToV3f(v3opos_t(decodedPos2.getX(), decodedPos2.getY(), decodedPos2.getZ()) * BS);
        
        v3f pos0, pos1, pos2;
        
        // Apply fscale scaling if needed (matching content_mapblock.cpp logic)
        if (m_data->fscale > 1) {
            int fscale = m_data->fscale;
            
            // Apply the same scaling transformation as in content_mapblock.cpp
            // First, shift to node center
            pos0 = base_pos0 + v3f(HBS, 0.0f, HBS);
            // Scale uniformly
            pos0 *= v3f((float)fscale, (float)fscale, (float)fscale);
            // Apply offset: -HBS for X/Z, and special Y offset calculation
            pos0 += v3f(-HBS, -HBS * (float)fscale + HBS + BS, -HBS);
            
            pos1 = base_pos1 + v3f(HBS, 0.0f, HBS);
            pos1 *= v3f((float)fscale, (float)fscale, (float)fscale);
            pos1 += v3f(-HBS, -HBS * (float)fscale + HBS + BS, -HBS);
            
            pos2 = base_pos2 + v3f(HBS, 0.0f, HBS);
            pos2 *= v3f((float)fscale, (float)fscale, (float)fscale);
            pos2 += v3f(-HBS, -HBS * (float)fscale + HBS + BS, -HBS);
        } else {
            pos0 = base_pos0;
            pos1 = base_pos1;
            pos2 = base_pos2;
        }
        
        // Apply the offset that the collector expects
        pos0 += offset;
        pos1 += offset;
        pos2 += offset;
        
        // Debug output for first few triangles
        if (triIdx < 5) {
            printf("PolyVoxMesher (smooth): Triangle %d: (%.2f,%.2f,%.2f) (%.2f,%.2f,%.2f) (%.2f,%.2f,%.2f)\n",
                   (int)triIdx,
                   pos0.X, pos0.Y, pos0.Z,
                   pos1.X, pos1.Y, pos1.Z,
                   pos2.X, pos2.Y, pos2.Z);
        }
        
        // Calculate normal (use the one from the vertex if available, otherwise calculate)
        v3f normal;
        // Check if normal has meaningful values (avoid division by zero)
        if (vertex0.normal.getX() != 0.0f || vertex0.normal.getY() != 0.0f || vertex0.normal.getZ() != 0.0f) {
            normal = v3f(vertex0.normal.getX(), vertex0.normal.getY(), vertex0.normal.getZ());
            if (normal.getLengthSQ() > 0.0f) {
                normal.normalize();
            } else {
                normal = v3f(0, 1, 0);
            }
        } else {
            // Calculate normal (cross product of two edges)
            v3f edge1 = pos1 - pos0;
            v3f edge2 = pos2 - pos0;
            normal = edge1.crossProduct(edge2);
            if (normal.getLengthSQ() > 0.0f) {
                normal.normalize();
            } else {
                normal = v3f(0, 1, 0);
            }
        }
        
        // Debug: Print normal for first few triangles
        if (triIdx < 5) {
            printf("PolyVoxMesher (smooth): Tri %d: normal=(%.3f,%.3f,%.3f)\n",
                   (int)triIdx, normal.X, normal.Y, normal.Z);
        }
        
        // Determine which node this triangle belongs to by looking at the vertex positions
        // For smooth meshes, we need to find the closest solid voxel
        v3pos_t base_node_pos = v3pos_t(
            (int)std::floor(base_pos0.X / BS + 0.5f),
            (int)std::floor(base_pos0.Y / BS + 0.5f),
            (int)std::floor(base_pos0.Z / BS + 0.5f)
        );
        
        // Adjust position based on normal to get the solid voxel
        // If normal points in positive direction, solid voxel is in negative direction
        v3pos_t triangle_node_pos = base_node_pos;
        const float normal_threshold = 0.1f;
        
        if (normal.X > normal_threshold) {
            triangle_node_pos.X -= 1; // Face points +X, solid is to -X
        } else if (normal.X < -normal_threshold) {
            triangle_node_pos.X += 1; // Face points -X, solid is to +X
        } else if (normal.Y > normal_threshold) {
            triangle_node_pos.Y -= 1; // Face points +Y, solid is to -Y
        } else if (normal.Y < -normal_threshold) {
            triangle_node_pos.Y += 1; // Face points -Y, solid is to +Y
        } else if (normal.Z > normal_threshold) {
            triangle_node_pos.Z -= 1; // Face points +Z, solid is to -Z
        } else if (normal.Z < -normal_threshold) {
            triangle_node_pos.Z += 1; // Face points -Z, solid is to +Z
        }
        // If normal is near zero or ambiguous, use the base position
        
        // Set up current node for this triangle
        cur_node.p = triangle_node_pos;
        cur_node.origin = oposToV3f(intToFloat(cur_node.p, BS));
        cur_node.n = m_data->m_vmanip.getNodeNoEx(blockpos_nodes + cur_node.p);
        cur_node.f = &nodedef->get(cur_node.n);
        
        // Debug: Print node info for first few triangles
        if (triIdx < 5) {
            printf("PolyVoxMesher (smooth): Tri %d: base=(%.1f,%.1f,%.1f) node=(%d,%d,%d) content=%d material=%d\n",
                   (int)triIdx, base_pos0.X, base_pos0.Y, base_pos0.Z,
                   triangle_node_pos.X, triangle_node_pos.Y, triangle_node_pos.Z,
                   (int)cur_node.n.getContent(), (int)nodeToMaterial(cur_node.n).getMaterial());
        }
        
        // Debug: Print lighting info
        if (triIdx < 3) {
            printf("PolyVoxMesher (smooth): Tri %d: smooth_lighting=%d content=%d\n",
                   (int)triIdx, m_data->m_smooth_lighting ? 1 : 0, (int)cur_node.n.getContent());
        }
        
        // Get lighting for this node
        if (m_data->m_smooth_lighting) {
            getSmoothLightFrame();
            cur_node.lcolor = blendLightColor(v3f(0, 0, 0)); // center of node
            if (triIdx < 3) {
                printf("PolyVoxMesher (smooth): Tri %d: smooth light color=(%d,%d,%d,%d)\n",
                       (int)triIdx,
                       cur_node.lcolor.getAlpha(), cur_node.lcolor.getRed(), 
                       cur_node.lcolor.getGreen(), cur_node.lcolor.getBlue());
            }
        } else {
            auto light = LightPair(getInteriorLight(cur_node.n, 0, nodedef));
            cur_node.lcolor = encode_light(light, cur_node.f->light_source);
            if (triIdx < 3) {
                printf("PolyVoxMesher (smooth): Tri %d: standard light=(%d,%d) color=(%d,%d,%d,%d)\n",
                       (int)triIdx, light.lightDay, light.lightNight,
                       cur_node.lcolor.getAlpha(), cur_node.lcolor.getRed(), 
                       cur_node.lcolor.getGreen(), cur_node.lcolor.getBlue());
            }
        }
        
        // Determine face direction based on normal for proper tile selection
        v3pos_t face_dir(0, 1, 0); // default to top face
        const float threshold = 0.5f;
        
        if (std::abs(normal.Y) > threshold && std::abs(normal.Y) > std::abs(normal.X) && std::abs(normal.Y) > std::abs(normal.Z)) {
            face_dir = v3pos_t(0, normal.Y > 0 ? 1 : -1, 0);
        } else if (std::abs(normal.X) > threshold && std::abs(normal.X) > std::abs(normal.Z)) {
            face_dir = v3pos_t(normal.X > 0 ? 1 : -1, 0, 0);
        } else if (std::abs(normal.Z) > threshold) {
            face_dir = v3pos_t(0, 0, normal.Z > 0 ? 1 : -1);
        }
        
        // Get the appropriate tile for this face direction
        TileSpec tile;
        
        // Use directional tile selection for all nodes (this ensures proper texture mapping)
        getNodeTile(cur_node.n, cur_node.p, face_dir, m_data, tile);
        
        // Apply backface culling for solid nodes (matching content_mapblock.cpp behavior)
        bool backface_culling = cur_node.f->drawtype == NDT_NORMAL || m_data->fscale > 1;
        if (backface_culling) {
            for (auto &layer : tile.layers) {
                if (layer.texture) {
                    layer.material_flags |= MATERIAL_FLAG_BACKFACE_CULLING;
                }
            }
        }
        
        // Apply tile animations and transformations (ported from content_mapblock.cpp)
        for (auto &layer : tile.layers) {
            if (layer.texture) {
                // Apply animation transformations
                if (layer.material_flags & MATERIAL_FLAG_ANIMATION) {
                    // Handle texture animation
                    // This is a simplified version - in reality you'd want to implement
                    // proper animation frame calculation like in the original
                }
                
                // Apply texture transforms
                if (layer.scale != 1) {
                    // Handle scaled textures
                }
                
                // Debug: Check for missing textures
                if (triIdx < 3 && !layer.texture) {
                    printf("PolyVoxMesher (smooth): Tri %d: WARNING - Missing texture for layer!\n", (int)triIdx);
                }
            }
        }
        
        // Debug: Print tile info for first few triangles
        if (triIdx < 3) {
            printf("PolyVoxMesher (smooth): Tri %d: face_dir=(%d,%d,%d) has_texture=%s scale=%d drawtype=%d\n",
                   (int)triIdx, face_dir.X, face_dir.Y, face_dir.Z,
                   tile.layers[0].texture ? "yes" : "no",
                   tile.layers[0].texture ? tile.layers[0].scale : 0,
                   (int)cur_node.f->drawtype);
        }
        
        // Generate texture coordinates based on the triangle vertices
        // Improved approach - generate UVs relative to the face and node position
        v2f uv0, uv1, uv2;
        
        // Calculate UV coordinates relative to the node's face
        // Use the vertex positions within the node's coordinate system
        v3f node_origin = cur_node.origin; // Center of the current node
        
        // Calculate relative positions within the node (normalized to [-0.5, 0.5])
        v3f rel_pos0 = (pos0 - node_origin) / BS;
        v3f rel_pos1 = (pos1 - node_origin) / BS;
        v3f rel_pos2 = (pos2 - node_origin) / BS;
        
        // Generate UV coordinates based on face normal orientation
        if (std::abs(normal.Y) > 0.5f) {
            // Top/bottom face - map X,Z coordinates
            uv0 = v2f(rel_pos0.X + 0.5f, rel_pos0.Z + 0.5f);
            uv1 = v2f(rel_pos1.X + 0.5f, rel_pos1.Z + 0.5f);
            uv2 = v2f(rel_pos2.X + 0.5f, rel_pos2.Z + 0.5f);
        } else if (std::abs(normal.X) > 0.5f) {
            // Side face X-aligned - map Z,Y coordinates
            uv0 = v2f(rel_pos0.Z + 0.5f, rel_pos0.Y + 0.5f);
            uv1 = v2f(rel_pos1.Z + 0.5f, rel_pos1.Y + 0.5f);
            uv2 = v2f(rel_pos2.Z + 0.5f, rel_pos2.Y + 0.5f);
        } else {
            // Side face Z-aligned - map X,Y coordinates
            uv0 = v2f(rel_pos0.X + 0.5f, rel_pos0.Y + 0.5f);
            uv1 = v2f(rel_pos1.X + 0.5f, rel_pos1.Y + 0.5f);
            uv2 = v2f(rel_pos2.X + 0.5f, rel_pos2.Y + 0.5f);
        }
        
        // Apply tile-specific transformations from content_mapblock.cpp approach
        // Handle texture scaling and other tile properties
        for (auto &layer : tile.layers) {
            if (layer.texture) {
                // Apply texture scaling
                if (layer.scale != 1) {
                    uv0.X *= layer.scale;
                    uv0.Y *= layer.scale;
                    uv1.X *= layer.scale;
                    uv1.Y *= layer.scale;
                    uv2.X *= layer.scale;
                    uv2.Y *= layer.scale;
                }
            }
        }
        
        // Debug: Print UV coordinates for first few triangles
        if (triIdx < 3) {
            printf("PolyVoxMesher (smooth): Tri %d: UVs=(%.3f,%.3f) (%.3f,%.3f) (%.3f,%.3f)\n",
                   (int)triIdx, uv0.X, uv0.Y, uv1.X, uv1.Y, uv2.X, uv2.Y);
        }
        
        // Create vertices with proper lighting and texture coordinates
        video::S3DVertex vertices[3];
        if (m_data->m_smooth_lighting) {
            vertices[0] = video::S3DVertex(pos0, normal, blendLightColor(pos0, normal), uv0);
            vertices[1] = video::S3DVertex(pos1, normal, blendLightColor(pos1, normal), uv1);
            vertices[2] = video::S3DVertex(pos2, normal, blendLightColor(pos2, normal), uv2);
        } else {
            video::SColor color = cur_node.lcolor;
            if (!cur_node.f->light_source) {
                applyFacesShading(color, normal);
            }
            vertices[0] = video::S3DVertex(pos0, normal, color, uv0);
            vertices[1] = video::S3DVertex(pos1, normal, color, uv1);
            vertices[2] = video::S3DVertex(pos2, normal, color, uv2);
        }
        
        // Debug: Print vertex colors for first few triangles
        if (triIdx < 3) {
            printf("PolyVoxMesher (smooth): Tri %d: colors=(%d,%d,%d,%d) (%d,%d,%d,%d) (%d,%d,%d,%d)\n",
                   (int)triIdx,
                   vertices[0].Color.getAlpha(), vertices[0].Color.getRed(), vertices[0].Color.getGreen(), vertices[0].Color.getBlue(),
                   vertices[1].Color.getAlpha(), vertices[1].Color.getRed(), vertices[1].Color.getGreen(), vertices[1].Color.getBlue(),
                   vertices[2].Color.getAlpha(), vertices[2].Color.getRed(), vertices[2].Color.getGreen(), vertices[2].Color.getBlue());
        }
        
        // Add triangle to collector
        u16 indices[3] = {0, 1, 2};
        m_collector->append(tile, vertices, 3, indices, 3);
    }
    
    printf("PolyVoxMesher (smooth): Finished processing %d triangles\n", (int)numTriangles);
}

void PolyVoxMesher::convertToCollector(const Mesh<Vertex<uint8_t>>& polyvoxMesh)
{
    if (polyvoxMesh.getNoOfIndices() == 0 || polyvoxMesh.getNoOfVertices() == 0)
        return;
    
    // Filter out small disconnected mesh components to prevent floating pieces
    // This helps eliminate noise and small isolated meshes from marching cubes
    const uint32_t minTriangleCount = 4; // Minimum triangles for a valid mesh component
    if (polyvoxMesh.getNoOfIndices() / 3 < minTriangleCount) {
        printf("PolyVoxMesher (smooth uint8): Skipping small mesh with %d triangles (below threshold %d)\n",
               (int)(polyvoxMesh.getNoOfIndices() / 3), (int)minTriangleCount);
        return;
    }
    
    // Calculate the offset that the original system expects
    // This matches the offset calculation in mapblock_mesh.cpp
    v3f offset = oposToV3f(intToFloat((m_data->m_blockpos - m_data->m_mesh_grid.getMeshPos(m_data->m_blockpos)) * MAP_BLOCKSIZE, BS));
    
    // Process triangles from the mesh
    const uint32_t numTriangles = polyvoxMesh.getNoOfIndices() / 3;
    
    // Debug output
    printf("PolyVoxMesher (smooth uint8): Processing mesh with %d vertices and %d indices (%d triangles), fscale=%d\n", 
           (int)polyvoxMesh.getNoOfVertices(), (int)polyvoxMesh.getNoOfIndices(), (int)numTriangles, m_data->fscale);
    
    for (uint32_t triIdx = 0; triIdx < numTriangles; triIdx++) {
        
        // Get the three vertices of this triangle
        uint32_t idx0 = polyvoxMesh.getIndex(triIdx * 3 + 0);
        uint32_t idx1 = polyvoxMesh.getIndex(triIdx * 3 + 1);
        uint32_t idx2 = polyvoxMesh.getIndex(triIdx * 3 + 2);
        
        if (idx0 >= polyvoxMesh.getNoOfVertices() || 
            idx1 >= polyvoxMesh.getNoOfVertices() || 
            idx2 >= polyvoxMesh.getNoOfVertices()) {
            continue;
        }
        
        auto vertex0 = polyvoxMesh.getVertex(idx0);
        auto vertex1 = polyvoxMesh.getVertex(idx1);
        auto vertex2 = polyvoxMesh.getVertex(idx2);
        
        // For decoded Vertex, position is already available
        Vector3DFloat decodedPos0 = vertex0.position;
        Vector3DFloat decodedPos1 = vertex1.position;
        Vector3DFloat decodedPos2 = vertex2.position;
        
        // Calculate vertex positions relative to mesh grid origin
        v3f base_pos0 = oposToV3f(v3opos_t(decodedPos0.getX(), decodedPos0.getY(), decodedPos0.getZ()) * BS);
        v3f base_pos1 = oposToV3f(v3opos_t(decodedPos1.getX(), decodedPos1.getY(), decodedPos1.getZ()) * BS);
        v3f base_pos2 = oposToV3f(v3opos_t(decodedPos2.getX(), decodedPos2.getY(), decodedPos2.getZ()) * BS);
        
        v3f pos0, pos1, pos2;
        
        // Apply fscale scaling if needed (matching content_mapblock.cpp logic)
        if (m_data->fscale > 1) {
            int fscale = m_data->fscale;
            
            // Apply the same scaling transformation as in content_mapblock.cpp
            // First, shift to node center
            pos0 = base_pos0 + v3f(HBS, 0.0f, HBS);
            // Scale uniformly
            pos0 *= v3f((float)fscale, (float)fscale, (float)fscale);
            // Apply offset: -HBS for X/Z, and special Y offset calculation
            pos0 += v3f(-HBS, -HBS * (float)fscale + HBS + BS, -HBS);
            
            pos1 = base_pos1 + v3f(HBS, 0.0f, HBS);
            pos1 *= v3f((float)fscale, (float)fscale, (float)fscale);
            pos1 += v3f(-HBS, -HBS * (float)fscale + HBS + BS, -HBS);
            
            pos2 = base_pos2 + v3f(HBS, 0.0f, HBS);
            pos2 *= v3f((float)fscale, (float)fscale, (float)fscale);
            pos2 += v3f(-HBS, -HBS * (float)fscale + HBS + BS, -HBS);
            
            printf("PolyVoxMesher (smooth uint8): fscale applied - pos0=(%.2f,%.2f,%.2f) base=(%.2f,%.2f,%.2f)\n",
                   pos0.X, pos0.Y, pos0.Z, base_pos0.X, base_pos0.Y, base_pos0.Z);
        } else {
            pos0 = base_pos0;
            pos1 = base_pos1;
            pos2 = base_pos2;
        }
        
        // Apply the offset that the collector expects
        pos0 += offset;
        pos1 += offset;
        pos2 += offset;
        
        // Debug output for first few triangles
        if (triIdx < 5) {
            printf("PolyVoxMesher (smooth uint8): Triangle %d: (%.2f,%.2f,%.2f) (%.2f,%.2f,%.2f) (%.2f,%.2f,%.2f)\n",
                   (int)triIdx,
                   pos0.X, pos0.Y, pos0.Z,
                   pos1.X, pos1.Y, pos1.Z,
                   pos2.X, pos2.Y, pos2.Z);
        }
        
        // Calculate normal using cross product (ensure correct winding order)
        v3f edge1 = pos1 - pos0;
        v3f edge2 = pos2 - pos0;
        v3f normal = edge1.crossProduct(edge2);
        
        // Check for degenerate triangle
        float area = normal.getLength();
        if (area < 0.0001f) {
            // Degenerate triangle, skip it
            continue;
        }
        
        // Normalize the normal vector
        normal.normalize();
        
        // Additional check: ensure normal isn't NaN or infinite
        if (!std::isfinite(normal.X) || !std::isfinite(normal.Y) || !std::isfinite(normal.Z)) {
            normal = v3f(0, 1, 0);
        }
        
        // Ensure normal has minimum magnitude to avoid culling issues
        if (normal.getLength() < 0.1f) {
            normal = v3f(0, 1, 0);
        }
        
        // Check if normal needs flipping for consistent outward-facing orientation
        // This helps with backface culling and visibility
        bool flipped = false;
        
        // For terrain meshes, we want to ensure normals are generally pointing outward
        // Only flip if the normal is strongly pointing in an inward direction
        // Be more conservative with flipping to preserve side face visibility
        if (normal.Y < -0.8f) {
            // Strongly downward-facing normal - flip it
            std::swap(pos0, pos1);
            normal = -normal;
            flipped = true;
        }
        // Don't flip side faces aggressively - let them render as-is
        // Side faces with mixed X/Y/Z components should remain visible
        
        // Debug: Print information about side face normals
        if (triIdx < 10) {
            float absX = std::abs(normal.X);
            float absY = std::abs(normal.Y);
            float absZ = std::abs(normal.Z);
            printf("PolyVoxMesher (smooth uint8): Tri %d: normal=(%.3f,%.3f,%.3f) abs=(%.3f,%.3f,%.3f)\n",
                   (int)triIdx, normal.X, normal.Y, normal.Z, absX, absY, absZ);
        }
        
        // Debug: Print normal for first few triangles
        if (triIdx < 10) {
            printf("PolyVoxMesher (smooth uint8): Tri %d: normal=(%.3f,%.3f,%.3f) flipped=%d\n",
                   (int)triIdx, normal.X, normal.Y, normal.Z, flipped ? 1 : 0);
        }
        
        // For uint8_t data, we need to map back to material for node lookup
        // Use the first vertex position to determine which node this triangle belongs to
        v3pos_t base_node_pos = v3pos_t(
            (int)std::floor(base_pos0.X / BS + 0.5f),
            (int)std::floor(base_pos0.Y / BS + 0.5f),
            (int)std::floor(base_pos0.Z / BS + 0.5f)
        );
        
        // Debug: Print base node position
        if (triIdx < 3) {
            printf("PolyVoxMesher (smooth uint8): Tri %d: base_node_pos=(%d,%d,%d) base_pos=(%.1f,%.1f,%.1f)\n",
                   (int)triIdx, base_node_pos.X, base_node_pos.Y, base_node_pos.Z,
                   base_pos0.X, base_pos0.Y, base_pos0.Z);
        }
        
        // Adjust position based on normal to get the solid voxel
        // If normal points in positive direction, solid voxel is in negative direction
        v3pos_t triangle_node_pos = base_node_pos;
        const float normal_threshold = 0.1f;
        
        if (normal.X > normal_threshold) {
            triangle_node_pos.X -= 1; // Face points +X, solid is to -X
        } else if (normal.X < -normal_threshold) {
            triangle_node_pos.X += 1; // Face points -X, solid is to +X
        } else if (normal.Y > normal_threshold) {
            triangle_node_pos.Y -= 1; // Face points +Y, solid is to -Y
        } else if (normal.Y < -normal_threshold) {
            triangle_node_pos.Y += 1; // Face points -Y, solid is to +Y
        } else if (normal.Z > normal_threshold) {
            triangle_node_pos.Z -= 1; // Face points +Z, solid is to -Z
        } else if (normal.Z < -normal_threshold) {
            triangle_node_pos.Z += 1; // Face points -Z, solid is to +Z
        }
        // If normal is near zero or ambiguous, use the base position
        
        // Set up current node for this triangle
        cur_node.p = triangle_node_pos;
        cur_node.origin = oposToV3f(intToFloat(cur_node.p, BS));
        cur_node.n = m_data->m_vmanip.getNodeNoEx(blockpos_nodes + cur_node.p);
        cur_node.f = &nodedef->get(cur_node.n);
        
        // Debug: Print node lookup result
        if (triIdx < 3) {
            printf("PolyVoxMesher (smooth uint8): Tri %d: lookup node=(%d,%d,%d) content=%d\n",
                   (int)triIdx, cur_node.p.X, cur_node.p.Y, cur_node.p.Z,
                   (int)cur_node.n.getContent());
        }
        
        // Debug: Print node info for first few triangles
        if (triIdx < 5) {
            printf("PolyVoxMesher (smooth uint8): Tri %d: base=(%.1f,%.1f,%.1f) node=(%d,%d,%d) content=%d material=%d\n",
                   (int)triIdx, base_pos0.X, base_pos0.Y, base_pos0.Z,
                   triangle_node_pos.X, triangle_node_pos.Y, triangle_node_pos.Z,
                   (int)cur_node.n.getContent(), (int)cur_node.n.getContent());
        }
        
        // Debug: Print lighting info
        if (triIdx < 3) {
            printf("PolyVoxMesher (smooth uint8): Tri %d: smooth_lighting=%d content=%d\n",
                   (int)triIdx, m_data->m_smooth_lighting ? 1 : 0, (int)cur_node.n.getContent());
        }
        
        // Debug: Print node content and features
        if (triIdx < 3) {
            printf("PolyVoxMesher (smooth uint8): Tri %d: node content=%d drawtype=%d light_source=%d\n",
                   (int)triIdx, (int)cur_node.n.getContent(), (int)cur_node.f->drawtype, (int)cur_node.f->light_source);
        }
        
        // Get lighting for this node
        if (m_data->m_smooth_lighting) {
            getSmoothLightFrame();
            cur_node.lcolor = blendLightColor(v3f(0, 0, 0)); // center of node
            if (triIdx < 3) {
                printf("PolyVoxMesher (smooth uint8): Tri %d: smooth light color=(%d,%d,%d,%d)\n",
                       (int)triIdx,
                       cur_node.lcolor.getAlpha(), cur_node.lcolor.getRed(), 
                       cur_node.lcolor.getGreen(), cur_node.lcolor.getBlue());
            }
        } else {
            auto light = LightPair(getInteriorLight(cur_node.n, 0, nodedef));
            if (triIdx < 3) {
                printf("PolyVoxMesher (smooth uint8): Tri %d: raw light=(%d,%d)\n",
                       (int)triIdx, light.lightDay, light.lightNight);
            }
            cur_node.lcolor = encode_light(light, cur_node.f->light_source);
            if (triIdx < 3) {
                printf("PolyVoxMesher (smooth uint8): Tri %d: encoded color=(%d,%d,%d,%d) light_source=%d\n",
                       (int)triIdx,
                       cur_node.lcolor.getAlpha(), cur_node.lcolor.getRed(), 
                       cur_node.lcolor.getGreen(), cur_node.lcolor.getBlue(),
                       (int)cur_node.f->light_source);
            }
        }
        
        // Determine face direction based on normal for proper tile selection
        v3pos_t face_dir(0, 1, 0); // default to top face
        const float threshold = 0.5f;
        
        if (std::abs(normal.Y) > threshold && std::abs(normal.Y) > std::abs(normal.X) && std::abs(normal.Y) > std::abs(normal.Z)) {
            face_dir = v3pos_t(0, normal.Y > 0 ? 1 : -1, 0);
        } else if (std::abs(normal.X) > threshold && std::abs(normal.X) > std::abs(normal.Z)) {
            face_dir = v3pos_t(normal.X > 0 ? 1 : -1, 0, 0);
        } else if (std::abs(normal.Z) > threshold) {
            face_dir = v3pos_t(0, 0, normal.Z > 0 ? 1 : -1);
        }
        
        // Get the appropriate tile for this face direction
        TileSpec tile;
        
        // Use directional tile selection for all nodes (this ensures proper texture mapping)
        getNodeTile(cur_node.n, cur_node.p, face_dir, m_data, tile);
        
        // Apply backface culling for solid nodes (matching content_mapblock.cpp behavior)
        bool backface_culling = cur_node.f->drawtype == NDT_NORMAL || m_data->fscale > 1;
        if (backface_culling) {
            for (auto &layer : tile.layers) {
                if (layer.texture) {
                    layer.material_flags |= MATERIAL_FLAG_BACKFACE_CULLING;
                }
            }
        }
        
        // Apply tile animations and transformations (ported from content_mapblock.cpp)
        for (auto &layer : tile.layers) {
            if (layer.texture) {
                // Apply animation transformations
                if (layer.material_flags & MATERIAL_FLAG_ANIMATION) {
                    // Handle texture animation
                    // This is a simplified version - in reality you'd want to implement
                    // proper animation frame calculation like in the original
                }
                
                // Apply texture transforms
                if (layer.scale != 1) {
                    // Handle scaled textures
                }
                
                // Debug: Check for missing textures
                if (triIdx < 3 && !layer.texture) {
                    printf("PolyVoxMesher (smooth uint8): Tri %d: WARNING - Missing texture for layer!\n", (int)triIdx);
                }
            }
        }
        
        // Debug: Print tile info for first few triangles
        if (triIdx < 3) {
            printf("PolyVoxMesher (smooth uint8): Tri %d: face_dir=(%d,%d,%d) has_texture=%s scale=%d drawtype=%d\n",
                   (int)triIdx, face_dir.X, face_dir.Y, face_dir.Z,
                   tile.layers[0].texture ? "yes" : "no",
                   tile.layers[0].texture ? tile.layers[0].scale : 0,
                   (int)cur_node.f->drawtype);
        }
        
        // Generate texture coordinates based on the triangle vertices
        // Improved approach - generate UVs relative to the face and node position
        v2f uv0, uv1, uv2;
        
        // Calculate UV coordinates relative to the node's face
        // Use the vertex positions within the node's coordinate system
        v3f node_origin = cur_node.origin; // Center of the current node
        
        // Calculate relative positions within the node (normalized to [-0.5, 0.5])
        v3f rel_pos0 = (pos0 - node_origin) / BS;
        v3f rel_pos1 = (pos1 - node_origin) / BS;
        v3f rel_pos2 = (pos2 - node_origin) / BS;
        
        // Generate UV coordinates based on face normal orientation
        if (std::abs(normal.Y) > 0.5f) {
            // Top/bottom face - map X,Z coordinates
            uv0 = v2f(rel_pos0.X + 0.5f, rel_pos0.Z + 0.5f);
            uv1 = v2f(rel_pos1.X + 0.5f, rel_pos1.Z + 0.5f);
            uv2 = v2f(rel_pos2.X + 0.5f, rel_pos2.Z + 0.5f);
        } else if (std::abs(normal.X) > 0.5f) {
            // Side face X-aligned - map Z,Y coordinates
            uv0 = v2f(rel_pos0.Z + 0.5f, rel_pos0.Y + 0.5f);
            uv1 = v2f(rel_pos1.Z + 0.5f, rel_pos1.Y + 0.5f);
            uv2 = v2f(rel_pos2.Z + 0.5f, rel_pos2.Y + 0.5f);
        } else {
            // Side face Z-aligned - map X,Y coordinates
            uv0 = v2f(rel_pos0.X + 0.5f, rel_pos0.Y + 0.5f);
            uv1 = v2f(rel_pos1.X + 0.5f, rel_pos1.Y + 0.5f);
            uv2 = v2f(rel_pos2.X + 0.5f, rel_pos2.Y + 0.5f);
        }
        
        // Apply tile-specific transformations from content_mapblock.cpp approach
        // Handle texture scaling and other tile properties
        for (auto &layer : tile.layers) {
            if (layer.texture) {
                // Apply texture scaling
                if (layer.scale != 1) {
                    uv0.X *= layer.scale;
                    uv0.Y *= layer.scale;
                    uv1.X *= layer.scale;
                    uv1.Y *= layer.scale;
                    uv2.X *= layer.scale;
                    uv2.Y *= layer.scale;
                }
            }
        }
        
        // If we flipped the positions, we also need to flip the UVs
        if (flipped) {
            std::swap(uv0, uv1);
        }
        
        // Debug: Print UV coordinates for first few triangles
        if (triIdx < 3) {
            printf("PolyVoxMesher (smooth uint8): Tri %d: UVs=(%.3f,%.3f) (%.3f,%.3f) (%.3f,%.3f)\n",
                   (int)triIdx, uv0.X, uv0.Y, uv1.X, uv1.Y, uv2.X, uv2.Y);
        }
        
        // Create vertices with proper lighting and texture coordinates
        video::S3DVertex vertices[3];
        if (m_data->m_smooth_lighting) {
            vertices[0] = video::S3DVertex(pos0, normal, blendLightColor(pos0, normal), uv0);
            vertices[1] = video::S3DVertex(pos1, normal, blendLightColor(pos1, normal), uv1);
            vertices[2] = video::S3DVertex(pos2, normal, blendLightColor(pos2, normal), uv2);
        } else {
            video::SColor color = cur_node.lcolor;
            if (!cur_node.f->light_source) {
                applyFacesShading(color, normal);
            }
            vertices[0] = video::S3DVertex(pos0, normal, color, uv0);
            vertices[1] = video::S3DVertex(pos1, normal, color, uv1);
            vertices[2] = video::S3DVertex(pos2, normal, color, uv2);
        }
        
        // Debug: Print vertex colors for first few triangles
        if (triIdx < 3) {
            printf("PolyVoxMesher (smooth uint8): Tri %d: colors=(%d,%d,%d,%d) (%d,%d,%d,%d) (%d,%d,%d,%d)\n",
                   (int)triIdx,
                   vertices[0].Color.getAlpha(), vertices[0].Color.getRed(), vertices[0].Color.getGreen(), vertices[0].Color.getBlue(),
                   vertices[1].Color.getAlpha(), vertices[1].Color.getRed(), vertices[1].Color.getGreen(), vertices[1].Color.getBlue(),
                   vertices[2].Color.getAlpha(), vertices[2].Color.getRed(), vertices[2].Color.getGreen(), vertices[2].Color.getBlue());
        }
        
        // Add triangle to collector
        u16 indices[3] = {0, 1, 2};
        m_collector->append(tile, vertices, 3, indices, 3);
    }
    
    printf("PolyVoxMesher (smooth uint8): Finished processing %d triangles\n", (int)numTriangles);
}

Material8 PolyVoxMesher::nodeToMaterial(const MapNode& node)
{
    // Convert Freeminer node content to PolyVox material
    content_t content = node.getContent();
    
    // Air nodes should be material 0 (empty) - these won't generate faces
    if (content == CONTENT_AIR || content == CONTENT_IGNORE) {
        return Material8(0);
    }
    
    // When fscale > 1, only generate meshes for solid-like nodes
    if (m_data->fscale > 1) {
        const ContentFeatures& features = m_data->m_nodedef->get(content);
        // Only generate meshes for normal solid blocks and liquids when fscale > 1
        // This matches the original behavior where fscale > 1 simplifies rendering
        if (features.drawtype != NDT_NORMAL && features.drawtype != NDT_LIQUID) {
            return Material8(0); // Treat as air/empty
        }
    }
    
    // Use the content ID directly as the material ID (but clamp to reasonable range)
    // This ensures different node types get different materials and thus different textures
    // Material8 can hold values 0-255, so we need to map content_t to this range
    uint8_t materialId = static_cast<uint8_t>(content % 255) + 1; // +1 to avoid 0 (air)
    return Material8(materialId);
}

MapNode PolyVoxMesher::materialToNode(const Material8& material)
{
    // Convert PolyVox material back to Freeminer node
    uint8_t materialId = material.getMaterial();
    
    // Air/empty material
    if (materialId == 0) {
        return MapNode(CONTENT_AIR);
    }
    
    // For now, return a simple solid node (content 2)
    return MapNode(2);
}

// Gets the base lighting values for a node (copied from content_mapblock.cpp)
void PolyVoxMesher::getSmoothLightFrame()
{
    for (int k = 0; k < 8; ++k)
        cur_node.lframe.sunlight[k] = false;
    for (int k = 0; k < 8; ++k) {
        LightPair light(getSmoothLightTransparent(blockpos_nodes + cur_node.p, light_dirs[k], m_data));
        cur_node.lframe.lightsDay[k] = light.lightDay;
        cur_node.lframe.lightsNight[k] = light.lightNight;
        // If there is direct sunlight and no ambient occlusion at some corner,
        // mark the vertical edge (top and bottom corners) containing it.
        if (light.lightDay == 255) {
            cur_node.lframe.sunlight[k] = true;
            cur_node.lframe.sunlight[k ^ 2] = true;
        }
    }
}

// Calculates vertex light level (adapted from content_mapblock.cpp)
LightInfo PolyVoxMesher::blendLight(const v3f &vertex_pos)
{
    // Light levels at (logical) node corners are known. Here,
    // trilinear interpolation is used to calculate light level
    // at a given point in the node.
    const float SMOOTH_LIGHTING_OVERSIZE = 1.0;
    f32 x = core::clamp(vertex_pos.X / BS + 0.5, 0.0 - SMOOTH_LIGHTING_OVERSIZE, 1.0 + SMOOTH_LIGHTING_OVERSIZE);
    f32 y = core::clamp(vertex_pos.Y / BS + 0.5, 0.0 - SMOOTH_LIGHTING_OVERSIZE, 1.0 + SMOOTH_LIGHTING_OVERSIZE);
    f32 z = core::clamp(vertex_pos.Z / BS + 0.5, 0.0 - SMOOTH_LIGHTING_OVERSIZE, 1.0 + SMOOTH_LIGHTING_OVERSIZE);
    f32 lightDay = 0.0; // daylight
    f32 lightNight = 0.0;
    f32 lightBoosted = 0.0; // daylight + direct sunlight, if any
    for (int k = 0; k < 8; ++k) {
        f32 dx = (k & 4) ? x : 1 - x;
        f32 dy = (k & 2) ? y : 1 - y;
        f32 dz = (k & 1) ? z : 1 - z;
        // Use direct sunlight (255), if any; use daylight otherwise.
        f32 light_boosted = cur_node.lframe.sunlight[k] ? 255 : cur_node.lframe.lightsDay[k];
        lightDay += dx * dy * dz * cur_node.lframe.lightsDay[k];
        lightNight += dx * dy * dz * cur_node.lframe.lightsNight[k];
        lightBoosted += dx * dy * dz * light_boosted;
    }
    return LightInfo{lightDay, lightNight, lightBoosted};
}

// Calculates vertex color to be used in mapblock mesh (copied from content_mapblock.cpp)
video::SColor PolyVoxMesher::blendLightColor(const v3f &vertex_pos)
{
    LightInfo light = blendLight(vertex_pos);
    return encode_light(light.getPair(), cur_node.f->light_source);
}

video::SColor PolyVoxMesher::blendLightColor(const v3f &vertex_pos, const v3f &vertex_normal)
{
    LightInfo light = blendLight(vertex_pos);
    video::SColor color = encode_light(light.getPair(MYMAX(0.0f, vertex_normal.Y)), cur_node.f->light_source);
    if (!cur_node.f->light_source)
        applyFacesShading(color, vertex_normal);
    return color;
}