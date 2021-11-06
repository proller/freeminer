#pragma once

#include "ISceneNode.h"
#include "IVideoDriver.h"
#include "camera.h"
#include "irr_v3d.h"
#include "constants.h"
class Client;
class Mapgen;
constexpr size_t FARMESH_MATERIAL_COUNT = 2;

class FarMesh : public scene::ISceneNode
{
public:
	FarMesh(
			scene::ISceneNode* parent,
			scene::ISceneManager* mgr,
			s32 id,
			//u64 seed,
			Client *client,Server * server
	);

	~FarMesh();

    //bool calc(v3POS blockpos_b, v3f camera_pos, v3f camera_dir, f32 camera_fov);
    //void draw(video::IVideoDriver* driver);
	virtual void render() override; 
	void update(v3f camera_pos, v3f camera_dir, f32 camera_fov,
	CameraMode camera_mode, f32 camera_pitch, f32 camera_yaw, v3POS m_camera_offset,
	 float brightness, s16 render_range);
virtual void OnRegisterSceneNode();

virtual const core::aabbox3d<f32>& getBoundingBox() const override
{                                                        
    //auto     m_box = core::aabbox3d<f32>(-BS*1000000,-BS*31000,-BS*1000000,        BS*1000000,BS*31000,BS*1000000);                      
    //auto   

    return m_box;                                        
}                                                        


private:
	video::SMaterial m_materials[FARMESH_MATERIAL_COUNT];
	core::aabbox3d<f32> m_box;
	float m_cloud_y;
	float m_brightness;
	//u64 m_seed;
	v3f m_camera_pos;
    v3f m_camera_dir;
    f32 m_camera_fov;
    f32 m_camera_pitch;
    f32 m_camera_yaw;
	float m_time;
	Client *m_client;
	s16 m_render_range;
	POS m_water_level  = 0;
	v3POS m_camera_offset;

Mapgen *mg = nullptr;
	//irr::core::line3df line;
};
