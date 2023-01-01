#pragma once

#include "IVideoDriver.h"
#include "client/camera.h"
#include "constants.h"
#include "irr_v3d.h"
#include "irrlichttypes.h"
#include "server.h"
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
			f32 camera_pitch, f32 camera_yaw, v3pos_t m_camera_offset, float brightness,
			s16 render_range);
	virtual void OnRegisterSceneNode() override;

	virtual const core::aabbox3d<f32> &getBoundingBox() const override { return m_box; }

    void CreateMesh();

private:
	video::SMaterial m_materials[FARMESH_MATERIAL_COUNT];
	core::aabbox3d<f32> m_box;
	float m_cloud_y;
	float m_brightness;
	v3f m_camera_pos = {-1337, -1337, -1337};
#if cache0
// v3pos_t m_camera_pos_aligned;
#endif
	#if cache_step
	std::vector<v3pos_t> m_camera_pos_aligned_by_step;
	#endif
	#if cache0
	v3pos_t m_camera_pos_aligned;
	#endif
	v3f m_camera_dir;
	f32 m_camera_fov;
	f32 m_camera_pitch;
	f32 m_camera_yaw;
	float m_time;
	Client *m_client;
	s16 m_render_range  = 16*16;
	uint32_t m_render_range_max;
	pos_t m_water_level = 0;
	v3pos_t m_camera_offset;
	constexpr static uint16_t grid_size_min = 16;
	//constexpr static uint16_t grid_size_max = 256;
	//constexpr static uint16_t grid_size_max = 64;
	constexpr static uint16_t grid_size_max_y = 64;
	//constexpr static uint16_t grid_size_max_y = 128;
	//constexpr static uint16_t grid_size_max_y = 16;
	constexpr static uint16_t grid_size_max_x = grid_size_max_y * 4;
	//constexpr static uint16_t grid_size_max = 16;
	std::array<uint16_t, grid_size_max_x*grid_size_max_y> process_order;
	// uint16_t grid_size = grid_size_min;
	//uint16_t grid_size = 64;
	//uint16_t grid_size = 256;
	//uint16_t grid_size = 	grid_size_max;
	uint16_t grid_size_x = 	grid_size_max_x;
	uint16_t grid_size_y = 	grid_size_max_y;
	//constexpr static uint16_t skip_x_num = 7;
	//constexpr static uint16_t skip_y_num = 7;
	//uint16_t m_skip_x_current = 0;
	//uint16_t m_skip_y_current = 1;
	// uint16_t grid_size = 32;
	// uint16_t grid_size = 8;
	Mapgen *mg = nullptr;
	unordered_map_v3pos<bool> mg_cache;

	struct ls_cache
	{
		uint32_t time = 0;
		unordered_map_v3pos<int> depth;
		unordered_map_v3pos<v3pos_t> pos;
		unordered_map_v3pos<pos_t> step_width; // debug only
		unordered_map_v3pos<pos_t> step;		 // debug only
	};
	unordered_map_v3pos<ls_cache> plane_cache;

	struct grid_result_item
	{
		v3pos_t pos;
		int depth;
		pos_t step_width; // debug
	};
	using result_array =
			std::array<std::array<grid_result_item, grid_size_max_y>, grid_size_max_x>;
	result_array grid_result_1, grid_result_2;
	result_array *grid_result_use = &grid_result_1;
	// result_array * grid_result_fill = &grid_result_2;
	result_array *grid_result_fill = &grid_result_1;

	uint16_t m_cycle_stop_x = 0, m_cycle_stop_y = 0, m_cycle_stop_i = 0;

	//irr::scene::IMesh* mesh = nullptr;
	irr::scene::SMesh* mesh = nullptr;
};
