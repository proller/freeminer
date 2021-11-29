#pragma once

#include "IVideoDriver.h"
#include "camera.h"
#include "constants.h"
#include "irr_v3d.h"
#include "util/unordered_map_hash.h"
#include <ISceneNode.h>
#include <cstdint>

#define cache0 1

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

    void CreateMesh();

private:
	video::SMaterial m_materials[FARMESH_MATERIAL_COUNT];
	core::aabbox3d<f32> m_box;
	float m_cloud_y;
	float m_brightness;
	v3f m_camera_pos;
#if cache0
// v3POS m_camera_pos_aligned;
#endif
	#if cache_step
	std::vector<v3POS> m_camera_pos_aligned_by_step;
	#endif
	#if cache0
	v3POS m_camera_pos_aligned;
	#endif
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
	//constexpr static uint16_t grid_size_max = 256;
	constexpr static uint16_t grid_size_max = 64;
	//constexpr static uint16_t grid_size_max = 16;
	std::array<uint16_t, grid_size_max*grid_size_max> process_order;
	// uint16_t grid_size = grid_size_min;
	//uint16_t grid_size = 64;
	//uint16_t grid_size = 256;
	uint16_t grid_size = 	grid_size_max;
	//constexpr static uint16_t skip_x_num = 7;
	//constexpr static uint16_t skip_y_num = 7;
	//uint16_t m_skip_x_current = 0;
	//uint16_t m_skip_y_current = 1;
	// uint16_t grid_size = 32;
	// uint16_t grid_size = 8;
	Mapgen *mg = nullptr;
	unordered_map_v3POS<bool> mg_cache;

	struct ls_cache
	{
		uint32_t time = 0;
		unordered_map_v3POS<POS> depth;
		unordered_map_v3POS<v3POS> pos;
		unordered_map_v3POS<POS> step_width; // debug only
		unordered_map_v3POS<POS> step;		 // debug only
	};
	unordered_map_v3POS<ls_cache> plane_cache;

	struct grid_result_item
	{
		v3POS pos;
		POS depth;
		POS step_width; // debug
	};
	using result_array =
			std::array<std::array<grid_result_item, grid_size_max>, grid_size_max>;
	result_array grid_result_1, grid_result_2;
	result_array *grid_result_use = &grid_result_1;
	// result_array * grid_result_fill = &grid_result_2;
	result_array *grid_result_fill = &grid_result_1;

	uint16_t m_cycle_stop_x = 0, m_cycle_stop_y = 0, m_cycle_stop_i = 0;

	//irr::scene::IMesh* mesh = nullptr;
	irr::scene::SMesh* mesh = nullptr;
};
