
#include "fm_farmesh.h"
#include "client.h"
#include "constants.h"
#include "emerge.h"
#include "irr_v3d.h"
#include "log.h"
#include "log_types.h"
#include "map.h"
#include "mapgen.h"
#include "mapnode.h"
#include "nodedef.h"
#include "server.h"
#include "util/unordered_map_hash.h"
#include <utility>

inline bool todo_mg(double x, double y, double z, double d = 10000, int ITR = 1,
                    int seed = 1) {
  return v3f(x - 1000, y - 11000, z - 2000).getLength() < d;
}

static bool isOccludedFarMesh(Map *map, v3s16 p0, v3s16 p1, float step,
                              float stepfac, float start_off, float end_off,
                              u32 needed_count, /*INodeDefManager *nodemgr,*/
                              unordered_map_v3POS<bool> &occlude_cache) {
  float d0 = (float)BS * p0.getDistanceFrom(p1);
  v3s16 u0 = p1 - p0;
  v3f uf = v3f(u0.X, u0.Y, u0.Z) * BS;
  uf.normalize();
  v3f p0f = v3f(p0.X, p0.Y, p0.Z) * BS;
  u32 count = 0;
  for (float s = start_off; s < d0 + end_off; s += step) {
    v3f pf = p0f + uf * s;
    v3s16 p = floatToInt(pf, BS);
    bool is_transparent = false;
    bool cache = true;
    if (occlude_cache.count(p)) {
      cache = false;
      is_transparent = occlude_cache[p];
    } else {

      if (MapNode n = map->getNodeTry(p);
          n.getContent() != CONTENT_IGNORE && n.getContent() != CONTENT_AIR) {
        is_transparent = false;
      } else {
        // TODO MAPGEN
        is_transparent = todo_mg(p.X, p.Y, p.Z, 10000);
      }
    }
    if (cache)
      occlude_cache[p] = is_transparent;
    if (!is_transparent) {
      count++;
      if (count >= needed_count)
        return true;
    }
    step *= stepfac;
  }
  return false;
}

FarMesh::FarMesh(scene::ISceneNode *parent, scene::ISceneManager *mgr, s32 id,
                 // u64 seed,
                 Client *client, Server *server)
    : scene::ISceneNode(parent, mgr, id),
      // m_seed(seed),
      m_camera_pos(0, 0, 0),
      // m_time(0),
      m_client(client) //,
                       // m_render_range(20*MAP_BLOCKSIZE)
{
  // dstream<<__FUNCTION_NAME<<std::endl;

  // video::IVideoDriver* driver = mgr->getVideoDriver();

  m_materials[0].setFlag(video::EMF_LIGHTING, false);
  m_materials[0].setFlag(video::EMF_BACK_FACE_CULLING, true);
  // m_materials[0].setFlag(video::EMF_BACK_FACE_CULLING, false);
  m_materials[0].setFlag(video::EMF_BILINEAR_FILTER, false);
  m_materials[0].setFlag(video::EMF_FOG_ENABLE, false);
  // m_materials[0].setFlag(video::EMF_ANTI_ALIASING, true);
  // m_materials[0].MaterialType = video::EMT_TRANSPARENT_VERTEX_ALPHA;
  m_materials[0].setFlag(video::EMF_FOG_ENABLE, true);

  m_materials[1].setFlag(video::EMF_LIGHTING, false);
  m_materials[1].setFlag(video::EMF_BACK_FACE_CULLING, false);
  m_materials[1].setFlag(video::EMF_BILINEAR_FILTER, false);
  m_materials[1].setFlag(video::EMF_FOG_ENABLE, false);
  // m_materials[1].setTexture(0, client->tsrc()->getTexture("treeprop.png"));
  m_materials[1].MaterialType = video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF;
  m_materials[1].setFlag(video::EMF_FOG_ENABLE, true);

  m_box = core::aabbox3d<f32>(-BS * MAX_MAP_GENERATION_LIMIT,
                              -BS * MAX_MAP_GENERATION_LIMIT,
                              -BS * MAX_MAP_GENERATION_LIMIT, BS * 1000000,
                              BS * 31000, BS * MAX_MAP_GENERATION_LIMIT);

  Settings user_settings;
  std::string test_mapmeta_path = ""; // makeMetaFile(false);
  MapSettingsManager map_settings_manager(&user_settings, test_mapmeta_path);

  Server *server_use = server                  ? server
                       : client->m_localserver ? client->m_localserver
                                               : nullptr;
  if (server_use) {
    verbosestream << " mgtype="
                  << server_use->getEmergeManager()->mgparams->mgtype << " wl="
                  << server_use->getEmergeManager()->mgparams->water_level
                  << "\n";
    mg = Mapgen::createMapgen(server_use->getEmergeManager()->mgparams->mgtype,
                              0, server_use->getEmergeManager()->mgparams,
                              server_use->getEmergeManager());
    m_water_level = server_use->getEmergeManager()->mgparams->water_level;
  }
}

FarMesh::~FarMesh() {
  // dstream<<__FUNCTION_NAME<<std::endl;
}

void FarMesh::OnRegisterSceneNode() {
  if (IsVisible) {
    // SceneManager->registerNodeForRendering(this, scene::ESNRP_TRANSPARENT);
    SceneManager->registerNodeForRendering(this, scene::ESNRP_SOLID);
    // SceneManager->registerNodeForRendering(this, scene::ESNRP_SKY_BOX);
  }

  ISceneNode::OnRegisterSceneNode();
}

void FarMesh::update(v3f camera_pos, v3f camera_dir, f32 camera_fov,
                     CameraMode camera_mode, f32 camera_pitch, f32 camera_yaw,
                     v3POS camera_offset, float brightness, s16 render_range) {

  m_camera_pos = camera_pos;
  m_camera_dir = camera_dir;
  m_camera_fov = camera_fov;
  m_camera_pitch = camera_pitch;
  m_camera_yaw = camera_yaw;
  m_camera_offset = camera_offset;

  /*
    errorstream << "update pos=" << camera_pos << " dir=" << camera_dir
                << " fov=" << camera_fov << " pitch=" << m_camera_pitch
                << " yaw=" << camera_yaw << std::endl;
  */

  /*
          const f32 block_max_radius = MAP_BLOCKSIZE * BS;

          v3s16 blockpos_nodes = blockpos_b * MAP_BLOCKSIZE;

          // Block center position
          v3f blockpos(
                          ((float)blockpos_nodes.X + MAP_BLOCKSIZE/2) * BS,
                          ((float)blockpos_nodes.Y + MAP_BLOCKSIZE/2) * BS,
                          ((float)blockpos_nodes.Z + MAP_BLOCKSIZE/2) * BS
          );
                  if (!camera_fov)
                  return true;
          // Adjust camera position, for purposes of computing the angle,
          // such that a block that has any portion visible with the
          // current camera position will have the center visible at the
          // adjusted postion
          f32 adjdist = block_max_radius / cos((M_PI - camera_fov) / 2);

          // Block position relative to adjusted camera
          v3f blockpos_adj = blockpos - (camera_pos - camera_dir * adjdist);

          // Distance in camera direction (+=front, -=back)
          f32 dforward = blockpos_adj.dotProduct(camera_dir);

          // Cosine of the angle between the camera direction
          // and the block direction (camera_dir is an unit vector)
          f32 cosangle = dforward / blockpos_adj.getLength();

          // If block is not in the field of view, skip it
          // HOTFIX: use sligthly increased angle (+10%) to fix too agressive
          // culling. Somebody have to find out whats wrong with the math here.
          // Previous value: camera_fov / 2
          if(cosangle < cos(camera_fov * 0.55))
                  return false;
  */
}

void FarMesh::render() {
  video::IVideoDriver *driver = SceneManager->getVideoDriver();
  driver->setTransform(video::ETS_WORLD, AbsoluteTransformation);

  const auto start = 2 << 7;

  // auto from_center = m_camera_pos + m_camera_dir /** BS */ * start;

  v3f dir_lu = v3f(0, 0, 1);
  dir_lu.rotateXZBy(irr::core::radToDeg(m_camera_fov / 2));
  dir_lu.rotateYZBy(irr::core::radToDeg(-m_camera_fov / 2));
  dir_lu.rotateYZBy(m_camera_yaw);
  dir_lu.rotateXZBy(m_camera_pitch);

  v3f dir_ld = v3f(0, 0, 1);
  dir_ld.rotateXZBy(irr::core::radToDeg(m_camera_fov / 2));
  dir_ld.rotateYZBy(irr::core::radToDeg(m_camera_fov / 2));
  dir_ld.rotateYZBy(m_camera_yaw);
  dir_ld.rotateXZBy(m_camera_pitch);
  v3f dir_rd = v3f(0, 0, 1);
  dir_rd.rotateXZBy(irr::core::radToDeg(-m_camera_fov / 2));
  dir_rd.rotateYZBy(irr::core::radToDeg(m_camera_fov / 2));
  dir_rd.rotateYZBy(m_camera_yaw);
  dir_rd.rotateXZBy(m_camera_pitch);

  driver->setTransform(irr::video::ETS_WORLD, irr::core::IdentityMatrix);
  irr::video::SMaterial material;
  material.Lighting = false;
  material.BackfaceCulling = false;
  material.ZBuffer = irr::video::ECFN_ALWAYS;
  driver->setMaterial(material);

  const int depth_steps = 512 + 100; // TODO CALC  256+128+64+32+16+8+4+2+1+?
  const int grid_size = 32;          // 255;
  int depth = 255;
  unordered_map_v2POS<std::pair<bool, v3POS>> grid_cache;

  for (short step_num = 0; step_num < depth_steps; ++step_num) {
    auto pos_ld = dir_ld * depth + m_camera_pos;
    auto pos_rd = dir_rd * depth + m_camera_pos;
    auto pos_lu = dir_lu * depth + m_camera_pos;
    auto step_r = (pos_rd - pos_ld) / grid_size;
    auto step_u = (pos_lu - pos_ld) / grid_size;
    auto step_width = (int(step_r.getLength()) >> 1 << 1);
    /*
        errorstream << " step_num=" << step_num << " depth=" << depth
                    << " pos_ld=" << pos_ld << " pos_rd=" << pos_rd
                    << " step_r=" << step_r << " srl=" << step_r.getLength()
                    << " sN=" << (int(step_r.getLength()) >> 1 << 1)
                    << " step_u=" << step_u << " sul=" << step_u.getLength()
                    << "\n";
    */

    for (short y = 0; y < grid_size; ++y) {
      auto pos_l = pos_ld + (step_u * y);
      // errorstream << "pos_l=" << pos_l << "\n";

      for (short x = 0; x < grid_size; ++x) {
        auto &cache = grid_cache[{x, y}];
        if (cache.first)
          continue;
        auto pos = pos_l + (step_r * x);
        v3POS pos_int = floatToInt(pos, BS);

        if (pos_int.Y < m_water_level) {
          cache.first = true;
          cache.second = pos_int;
        } else if (mg && mg->visible(pos_int.X, pos_int.Y, pos_int.Z)) {
          cache.first = true;
          cache.second = pos_int;
        }
        if (!cache.first)
          continue;
        // errorstream << " x=" << x << " pos=" << pos <<  " step_r="  << "
        // depth=" << depth << " step_width=" << step_width << "\n";
        pos -= intToFloat(m_camera_offset, BS);
        driver->draw3DLine(pos, pos + step_width * v3f(1, 0, 0),
                           irr::video::SColor(255, 255, 200, step_num));
      }
    }
    depth += step_width;
    if (depth > MAX_MAP_GENERATION_LIMIT * 2 ||
        depth < -MAX_MAP_GENERATION_LIMIT * 2)
      break;
  }
}