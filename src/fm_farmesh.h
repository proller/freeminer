#pragma once

#include "ISceneNode.h"
#include "IVideoDriver.h"
#include "camera.h"
#include "constants.h"
#include "irr_v3d.h"
#include "util/unordered_map_hash.h"
class Client;
class Mapgen;
constexpr size_t FARMESH_MATERIAL_COUNT = 2;

class FarMesh : public scene::ISceneNode
{
public:
	FarMesh(scene::ISceneNode *parent, scene::ISceneManager *mgr, s32 id, Client *client,
			Server *server);

	~FarMesh();

	virtual void render() override;
	void update(v3f camera_pos, v3f camera_dir, f32 camera_fov, CameraMode camera_mode,
			f32 camera_pitch, f32 camera_yaw, v3POS m_camera_offset, float brightness,
			s16 render_range);
	virtual void OnRegisterSceneNode();

	virtual const core::aabbox3d<f32> &getBoundingBox() const override { return m_box; }

private:
	video::SMaterial m_materials[FARMESH_MATERIAL_COUNT];
	core::aabbox3d<f32> m_box;
	float m_cloud_y;
	float m_brightness;
	v3f m_camera_pos;
	v3POS m_camera_pos_aligned;
	v3f m_camera_dir;
	f32 m_camera_fov;
	f32 m_camera_pitch;
	f32 m_camera_yaw;
	float m_time;
	Client *m_client;
	s16 m_render_range;
	uint32_t m_render_range_max;
	POS m_water_level = 0;
	v3POS m_camera_offset;
	constexpr static uint16_t grid_size_min = 16;
	constexpr static uint16_t grid_size_max = 256;
	//uint16_t grid_size = grid_size_min;
	uint16_t grid_size = 32;
	Mapgen *mg = nullptr;
	unordered_map_v3POS<bool> mg_cache;

	struct ls_cache
	{
		uint32_t time = 0;
		unordered_map_v3POS<POS> depth;
	};
	unordered_map_v3POS<ls_cache> plane_cache;
};
