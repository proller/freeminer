#pragma once

#include "IVideoDriver.h"
#include "client/camera.h"
#include "constants.h"
#include "irr_v3d.h"
#include "irrlichttypes.h"
#include "server.h"
#include "threading/async.h"
#include "util/unordered_map_hash.h"
#include <ISceneNode.h>
#include <cstdint>

#define cache0 1

class Client;
class Mapgen;
constexpr size_t FARMESH_MATERIAL_COUNT = 2;

class FarContainer : public NodeContainer
{

public:
	FarContainer();
	MapNode &getNodeRefUnsafe(const v3pos_t &p) override;
	MapNode getNodeNoExNoEmerge(const v3pos_t &p) override;
	MapNode getNodeNoEx(const v3pos_t &p) override;
	Mapgen *m_mg = nullptr;
	MapNode visible_node;
	MapNode water_node;
	pos_t m_water_level = 0;
};

class FarMesh
#if 0
: public scene::ISceneNode
#endif
{
public:
	FarMesh(scene::ISceneNode *parent, scene::ISceneManager *mgr, s32 id, Client *client,
			Server *server);

	~FarMesh();

	void update(v3f camera_pos, v3f camera_dir, f32 camera_fov, CameraMode camera_mode,
			f32 camera_pitch, f32 camera_yaw, v3pos_t m_camera_offset, float brightness,
			s16 render_range);
#if 0 
	virtual void render() override;
	virtual void OnRegisterSceneNode() override;

	virtual const core::aabbox3d<f32> &getBoundingBox() const override { return m_box; }

	void CreateMesh();
#endif

	void makeFarBlock(const v3bpos_t &blockpos);
	void makeFarBlock6(const v3bpos_t &blockpos);

	void makeFarBlocks(const v3bpos_t &blockpos);

private:
	int m_make_far_blocks_last = 0;
	std::vector<v3bpos_t> m_make_far_blocks_list;

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
	pos_t m_render_range = 16 * MAP_BLOCKSIZE;
	uint32_t m_render_range_max;
	pos_t m_water_level = 0;
	v3pos_t m_camera_offset;
	//constexpr static uint16_t grid_size_min = 16;
	//constexpr static uint16_t grid_size_max = 256;
	//constexpr static uint16_t grid_size_max = 64;
	constexpr static uint16_t grid_size_max_y = 64;
	//constexpr static uint16_t grid_size_max_y = 128;
	//constexpr static uint16_t grid_size_max_y = 256;
	//constexpr static uint16_t grid_size_max_y = 16;
	//cylinder: constexpr static uint16_t grid_size_max_x = grid_size_max_y * 4;
	constexpr static uint16_t grid_size_max_x = grid_size_max_y;
	//constexpr static uint16_t grid_size_max = 16;
	std::array<uint16_t, grid_size_max_x * grid_size_max_y> process_order;
	// uint16_t grid_size = grid_size_min;
	//uint16_t grid_size = 64;
	//uint16_t grid_size = 256;
	//uint16_t grid_size = 	grid_size_max;
	static constexpr uint16_t grid_size_x = grid_size_max_x;
	static constexpr uint16_t grid_size_y = grid_size_max_y;
	static constexpr uint16_t grid_size_xy = grid_size_x * grid_size_y;
	//constexpr static uint16_t skip_x_num = 7;
	//constexpr static uint16_t skip_y_num = 7;
	//uint16_t m_skip_x_current = 0;
	//uint16_t m_skip_y_current = 1;
	// uint16_t grid_size = 32;
	// uint16_t grid_size = 8;
	Mapgen *mg = nullptr;
	static thread_local unordered_map_v3pos<bool> mg_cache;

#if 0
	struct ls_cache
	{
		uint32_t time = 0;
		unordered_map_v3pos<int> depth;
		unordered_map_v3pos<v3pos_t> pos;
		unordered_map_v3pos<pos_t> step_width; // debug only
		unordered_map_v3pos<pos_t> step;	   // debug only
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

	//irr::scene::IMesh* mesh = nullptr;
	irr::scene::SMesh *mesh = nullptr;

	uint16_t m_cycle_stop_x = 0, m_cycle_stop_y = 0, m_cycle_stop_i = 0;
#endif
	uint16_t m_cycle_stop_i = 0;

	FarContainer farcontainer;

	struct ray_cache
	{
		bool finished = false;
		bool visible = false;
		size_t step_num = 0;
	};
	using direction_cache = std::array<ray_cache, grid_size_xy>;
	std::array<direction_cache, 6> direction_caches;
	v3pos_t direction_caches_pos;
	struct plane_cache
	{
		int processed = -1;
	};
	std::array<plane_cache, 6> plane_caches;
	int go_direction(const size_t dir_n);
	std::array<async_step_runner, 6> async;
	int timestamp_complete = 0;
	int timestamp_clean = 0;
	bool complete_set = false;
	int planes_processed_last = 0;
};
