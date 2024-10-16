/*
client.h
Copyright (C) 2013 celeron55, Perttu Ahola <celeron55@gmail.com>
*/

/*
This file is part of Freeminer.

Freeminer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Freeminer  is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Freeminer.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef CLIENT_HEADER
#define CLIENT_HEADER

#include "network/connection.h"
#include "environment.h"
#include "irrlichttypes_extrabloated.h"
#include "threading/mutex.h"
#include <ostream>
#include <map>
#include <set>
#include <vector>
#include "clientobject.h"
#include "gamedef.h"
#include "inventorymanager.h"
#include "localplayer.h"
#include "hud.h"
#include "particles.h"

#include "threading/thread_pool.h"
#include "util/unordered_map_hash.h"
#include "msgpack_fix.h"

#include "network/networkpacket.h"

struct MeshMakeData;
class MapBlockMesh;
class IWritableTextureSource;
class IWritableShaderSource;
class IWritableItemDefManager;
class IWritableNodeDefManager;
//class IWritableCraftDefManager;
class ClientMediaDownloader;
struct MapDrawControl;
class MtEventManager;
struct PointedThing;
class Database;
class Server;
class Mapper;
struct MinimapMapblock;
class ChatBackend;
class Camera;

/*
struct QueuedMeshUpdate
{
	v3s16 p;
	MeshMakeData *data;
	bool ack_block_to_server;

	QueuedMeshUpdate();
	~QueuedMeshUpdate();
};
*/

enum LocalClientState {
	LC_Created,
	LC_Init,
	LC_Ready
};

/*
	A thread-safe queue of mesh update tasks
*/
class MeshUpdateQueue
{
public:
	MeshUpdateQueue();

	~MeshUpdateQueue();

	unsigned int addBlock(v3POS p, std::shared_ptr<MeshMakeData> data, bool urgent);
	std::shared_ptr<MeshMakeData> pop();

	concurrent_unordered_map<v3s16, bool, v3POSHash, v3POSEqual> m_process;

private:
	concurrent_map<unsigned int, unordered_map_v3POS<std::shared_ptr<MeshMakeData>>> m_queue;
	unordered_map_v3POS<unsigned int> m_ranges;
};

struct MeshUpdateResult
{
	v3s16 p;
	MapBlock::mesh_type mesh;

	MeshUpdateResult(v3POS & p_, MapBlock::mesh_type mesh_):
		p(p_),
		mesh(mesh_)
	{
	}
};

class MeshUpdateThread : public UpdateThread
{
private:
	MeshUpdateQueue m_queue_in;
 
protected:
	virtual void doUpdate();

public:

	MeshUpdateThread() : UpdateThread("Mesh") {}

	void enqueueUpdate(v3s16 p, std::shared_ptr<MeshMakeData> data,
			bool urgent);

	MutexedQueue<MeshUpdateResult> m_queue_out;

	v3s16 m_camera_offset;
	int id;
};

enum ClientEventType
{
	CE_NONE,
	CE_PLAYER_DAMAGE,
	CE_PLAYER_FORCE_MOVE,
	CE_DEATHSCREEN,
	CE_SHOW_FORMSPEC,
	CE_SPAWN_PARTICLE,
	CE_ADD_PARTICLESPAWNER,
	CE_DELETE_PARTICLESPAWNER,
	CE_HUDADD,
	CE_HUDRM,
	CE_HUDCHANGE,
	CE_SET_SKY,
	CE_OVERRIDE_DAY_NIGHT_RATIO,
};

struct ClientEvent
{
	ClientEventType type;
	union{
		//struct{
		//} none;
		struct{
			u8 amount;
		} player_damage;
		struct{
			f32 pitch;
			f32 yaw;
		} player_force_move;
		struct{
			bool set_camera_point_target;
			f32 camera_point_target_x;
			f32 camera_point_target_y;
			f32 camera_point_target_z;
		} deathscreen;
		struct{
			std::string *formspec;
			std::string *formname;
		} show_formspec;
		//struct{
		//} textures_updated;
		struct{
			v3f *pos;
			v3f *vel;
			v3f *acc;
			f32 expirationtime;
			f32 size;
			bool collisiondetection;
			bool vertical;
			std::string *texture;
		} spawn_particle;
		struct{
			u16 amount;
			f32 spawntime;
			v3f *minpos;
			v3f *maxpos;
			v3f *minvel;
			v3f *maxvel;
			v3f *minacc;
			v3f *maxacc;
			f32 minexptime;
			f32 maxexptime;
			f32 minsize;
			f32 maxsize;
			bool collisiondetection;
			bool vertical;
			std::string *texture;
			u32 id;
		} add_particlespawner;
		struct{
			u32 id;
		} delete_particlespawner;
		struct{
			u32 id;
			u8 type;
			v2f *pos;
			std::string *name;
			v2f *scale;
			std::string *text;
			u32 number;
			u32 item;
			u32 dir;
			v2f *align;
			v2f *offset;
			v3f *world_pos;
			v2s32 * size;
		} hudadd;
		struct{
			u32 id;
		} hudrm;
		struct{
			u32 id;
			HudElementStat stat;
			v2f *v2fdata;
			std::string *sdata;
			u32 data;
			v3f *v3fdata;
			v2s32 * v2s32data;
		} hudchange;
		struct{
			video::SColor *bgcolor;
			std::string *type;
			std::vector<std::string> *params;
		} set_sky;
		struct{
			bool do_override;
			float ratio_f;
		} override_day_night_ratio;
	};
};

/*
	Packet counter
*/

class PacketCounter
{
public:
	PacketCounter()
	{
	}

	void add(u16 command)
	{
		std::map<u16, u16>::iterator n = m_packets.find(command);
		if(n == m_packets.end())
		{
			m_packets[command] = 1;
		}
		else
		{
			n->second++;
		}
	}

	void clear()
	{
		for(std::map<u16, u16>::iterator
				i = m_packets.begin();
				i != m_packets.end(); ++i)
		{
			i->second = 0;
		}
	}

	void print(std::ostream &o)
	{
		for(std::map<u16, u16>::iterator
				i = m_packets.begin();
				i != m_packets.end(); ++i)
		{
			if (i->second)
			o<<"cmd "<<i->first
					<<" count "<<i->second
					<<std::endl;
		}
	}

private:
	// command, count
	std::map<u16, u16> m_packets;
};

class Client : public con::PeerHandler, public InventoryManager, public IGameDef
{
public:
	/*
		NOTE: Nothing is thread-safe here.
	*/

	Client(
			IrrlichtDevice *device,
			const char *playername,
			std::string password,
			bool is_simple_singleplayer_game,
			MapDrawControl &control,
			IWritableTextureSource *tsrc,
			IWritableShaderSource *shsrc,
			IWritableItemDefManager *itemdef,
			IWritableNodeDefManager *nodedef,
			ISoundManager *sound,
			MtEventManager *event,
			bool ipv6
	);

	~Client();

	/*
	 request all threads managed by client to be stopped
	 */
	void Stop();

	/*
		The name of the local player should already be set when
		calling this, as it is sent in the initialization.
	*/
	void connect(Address address,
			const std::string &address_name,
			bool is_local_server);

	/*
		Stuff that references the environment is valid only as
		long as this is not called. (eg. Players)
		If this throws a PeerNotFoundException, the connection has
		timed out.
	*/
	void step(float dtime);

	/*
	 * Command Handlers
	 */

	void handleCommand(NetworkPacket* pkt);

	void handleCommand_Null(NetworkPacket* pkt) {};
	void handleCommand_Deprecated(NetworkPacket* pkt);
	void handleCommand_Hello(NetworkPacket* pkt);
	void handleCommand_AuthAccept(NetworkPacket* pkt);
	void handleCommand_AcceptSudoMode(NetworkPacket* pkt);
	void handleCommand_DenySudoMode(NetworkPacket* pkt);
	void handleCommand_InitLegacy(NetworkPacket* pkt);
	void handleCommand_AccessDenied(NetworkPacket* pkt);
	void handleCommand_RemoveNode(NetworkPacket* pkt);
	void handleCommand_AddNode(NetworkPacket* pkt);
	void handleCommand_BlockData(NetworkPacket* pkt);
	void handleCommand_Inventory(NetworkPacket* pkt);
	void handleCommand_TimeOfDay(NetworkPacket* pkt);
	void handleCommand_ChatMessage(NetworkPacket* pkt);
	void handleCommand_ActiveObjectRemoveAdd(NetworkPacket* pkt);
	void handleCommand_ActiveObjectMessages(NetworkPacket* pkt);
	void handleCommand_Movement(NetworkPacket* pkt);
	void handleCommand_HP(NetworkPacket* pkt);
	void handleCommand_Breath(NetworkPacket* pkt);
	void handleCommand_MovePlayer(NetworkPacket* pkt);
	void handleCommand_PunchPlayer(NetworkPacket* pkt);
	void handleCommand_DeathScreen(NetworkPacket* pkt);
	void handleCommand_AnnounceMedia(NetworkPacket* pkt);
	void handleCommand_Media(NetworkPacket* pkt);
	void handleCommand_ToolDef(NetworkPacket* pkt);
	void handleCommand_NodeDef(NetworkPacket* pkt);
	void handleCommand_CraftItemDef(NetworkPacket* pkt);
	void handleCommand_ItemDef(NetworkPacket* pkt);
	void handleCommand_PlaySound(NetworkPacket* pkt);
	void handleCommand_StopSound(NetworkPacket* pkt);
	void handleCommand_Privileges(NetworkPacket* pkt);
	void handleCommand_InventoryFormSpec(NetworkPacket* pkt);
	void handleCommand_DetachedInventory(NetworkPacket* pkt);
	void handleCommand_ShowFormSpec(NetworkPacket* pkt);
	void handleCommand_SpawnParticle(NetworkPacket* pkt);
	void handleCommand_AddParticleSpawner(NetworkPacket* pkt);
	void handleCommand_DeleteParticleSpawner(NetworkPacket* pkt);
	void handleCommand_HudAdd(NetworkPacket* pkt);
	void handleCommand_HudRemove(NetworkPacket* pkt);
	void handleCommand_HudChange(NetworkPacket* pkt);
	void handleCommand_HudSetFlags(NetworkPacket* pkt);
	void handleCommand_HudSetParam(NetworkPacket* pkt);
	void handleCommand_HudSetSky(NetworkPacket* pkt);
	void handleCommand_OverrideDayNightRatio(NetworkPacket* pkt);
	void handleCommand_LocalPlayerAnimations(NetworkPacket* pkt);
	void handleCommand_EyeOffset(NetworkPacket* pkt);
	void handleCommand_SrpBytesSandB(NetworkPacket* pkt);

	void ProcessData(NetworkPacket *pkt);

	// Returns true if something was received
	bool AsyncProcessPacket();
	bool AsyncProcessData();
/*
	void Send(u16 channelnum, SharedBuffer<u8> data, bool reliable);
*/
	void Send(u16 channelnum, const msgpack::sbuffer &data, bool reliable);
	void Send(NetworkPacket* pkt);

	void interact(u8 action, const PointedThing& pointed);

	void sendNodemetaFields(v3s16 p, const std::string &formname,
		const StringMap &fields);
	void sendInventoryFields(const std::string &formname,
		const StringMap &fields);
	void sendInventoryAction(InventoryAction *a);
	void sendChatMessage(const std::string &message);
	void sendChangePassword(const std::string &oldpassword,
		const std::string &newpassword);
	void sendDamage(u8 damage);
	void sendBreath(u16 breath);
	void sendRespawn();
	void sendReady();

	ClientEnvironment& getEnv()
	{ return m_env; }

	// Causes urgent mesh updates (unlike Map::add/removeNodeWithEvent)
	void removeNode(v3s16 p, int fast = 0);
	void addNode(v3s16 p, MapNode n, bool remove_metadata = true, int fast = 0);

	void setPlayerControl(PlayerControl &control);

	void selectPlayerItem(u16 item);
	u16 getPlayerItem() const
	{ return m_playeritem; }
	u16 getPreviousPlayerItem() const
	{ return m_previous_playeritem; }

	// Returns true if the inventory of the local player has been
	// updated from the server. If it is true, it is set to false.
	bool getLocalInventoryUpdated();
	// Copies the inventory of the local player to parameter
	void getLocalInventory(Inventory &dst);

	/* InventoryManager interface */
	Inventory* getInventory(const InventoryLocation &loc);
	void inventoryAction(InventoryAction *a);

	// Gets closest object pointed by the shootline
	// Returns NULL if not found
	ClientActiveObject * getSelectedActiveObject(
			f32 max_d,
			v3f from_pos_f_on_map,
			core::line3d<f32> shootline_on_map
	);

	std::list<std::string> getConnectedPlayerNames();

	float getAnimationTime();

	int getCrackLevel();
	void setCrack(int level, v3s16 pos);

	u16 getHP();
	u16 getBreath();

	bool checkPrivilege(const std::string &priv)
	{ return (m_privileges.count(priv) != 0); }

	bool getChatMessage(std::string &message);
	void typeChatMessage(const std::string& message);

	u64 getMapSeed(){ return m_map_seed; }

	void addUpdateMeshTask(v3s16 blockpos, bool urgent=false, int step = 0);
	// Including blocks at appropriate edges
	void addUpdateMeshTaskWithEdge(v3POS blockpos, bool urgent = false);
	void addUpdateMeshTaskForNode(v3s16 nodepos, bool urgent=false);

	void updateMeshTimestampWithEdge(v3s16 blockpos);

	void updateCameraOffset(v3s16 camera_offset)
	{ m_mesh_update_thread.m_camera_offset = camera_offset; }

	// Get event from queue. CE_NONE is returned if queue is empty.
	ClientEvent getClientEvent();

	bool accessDenied()
	{ return m_access_denied; }

	bool reconnectRequested() { return m_access_denied_reconnect; }

	std::string accessDeniedReason()
	{ return m_access_denied_reason; }

	bool itemdefReceived()
	{ return m_itemdef_received; }
	bool nodedefReceived()
	{ return m_nodedef_received; }
	bool mediaReceived()
	{ return m_media_downloader == NULL; }

	u8 getProtoVersion()
	{ return m_proto_ver; }

	float mediaReceiveProgress();

	void afterContentReceived(IrrlichtDevice *device);

	float getRTT(void);
	float getCurRate(void);
	float getAvgRate(void);

	Mapper* getMapper ()
	{ return m_mapper; }

	void setCamera(Camera* camera)
	{ m_camera = camera; }

	Camera* getCamera ()
	{ return m_camera; }

	bool isMinimapDisabledByServer()
	{ return m_minimap_disabled_by_server; }

	// IGameDef interface
	virtual IItemDefManager* getItemDefManager();
	virtual INodeDefManager* getNodeDefManager();
	virtual ICraftDefManager* getCraftDefManager();
	virtual ITextureSource* getTextureSource();
	virtual IShaderSource* getShaderSource();
	virtual scene::ISceneManager* getSceneManager();
	virtual u16 allocateUnknownNodeId(const std::string &name);
	virtual ISoundManager* getSoundManager();
	virtual MtEventManager* getEventManager();
	virtual ParticleManager* getParticleManager();
	virtual bool checkLocalPrivilege(const std::string &priv)
	{ return checkPrivilege(priv); }
	virtual scene::IAnimatedMesh* getMesh(const std::string &filename);

	// The following set of functions is used by ClientMediaDownloader
	// Insert a media file appropriately into the appropriate manager
	bool loadMedia(const std::string &data, const std::string &filename);
	// Send a request for conventional media transfer
	void request_media(const std::vector<std::string> &file_requests);
	// Send a notification that no conventional media transfer is needed
	void received_media();

	LocalClientState getState() { return m_state; }

	void makeScreenshot(const std::string & name = "screenshot_", IrrlichtDevice *device = nullptr);
	
	ChatBackend *chat_backend;

private:

	// Virtual methods from con::PeerHandler
	void peerAdded(u16 peer_id);
	void deletingPeer(u16 peer_id, bool timeout);

	void initLocalMapSaving(const Address &address,
			const std::string &hostname,
			bool is_local_server);

	void ReceiveAll();
	bool Receive();

	void sendPlayerPos();
	// Send the item number 'item' as player item to the server
	void sendPlayerItem(u16 item);

	void deleteAuthData();
	// helper method shared with clientpackethandler
	static AuthMechanism choseAuthMech(const u32 mechs);

	void sendLegacyInit(const std::string &playerName, const std::string &playerPassword);
	void sendInit(const std::string &playerName);
	void startAuth(AuthMechanism chosen_auth_mechanism);
	void sendDeletedBlocks(std::vector<v3s16> &blocks);
	void sendGotBlocks(v3s16 block);
	void sendRemovedSounds(std::vector<s32> &soundList);

	// Helper function
	inline std::string getPlayerName()
	{ return m_env.getLocalPlayer()->getName(); }

	float m_packetcounter_timer;
	float m_connection_reinit_timer;
	float m_avg_rtt_timer;
	float m_playerpos_send_timer;
	float m_ignore_damage_timer; // Used after server moves player
	IntervalLimiter m_map_timer_and_unload_interval;

	IWritableTextureSource *m_tsrc;
	IWritableShaderSource *m_shsrc;
	IWritableItemDefManager *m_itemdef;
	IWritableNodeDefManager *m_nodedef;
	ISoundManager *m_sound;
	MtEventManager *m_event;

	MeshUpdateThread m_mesh_update_thread;
private:
	ClientEnvironment m_env;
	ParticleManager m_particle_manager;
public:
	con::Connection m_con;
private:
	IrrlichtDevice *m_device;
	Camera *m_camera;
	Mapper *m_mapper;
	bool m_minimap_disabled_by_server;
	// Server serialization version
	u8 m_server_ser_ver;

	// Used version of the protocol with server
	// Values smaller than 25 only mean they are smaller than 25,
	// and aren't accurate. We simply just don't know, because
	// the server didn't send the version back then.
	// If 0, server init hasn't been received yet.
	u8 m_proto_ver;

	u16 m_playeritem;
	u16 m_previous_playeritem;
	bool m_inventory_updated;
	Inventory *m_inventory_from_server;
	float m_inventory_from_server_age;
	PacketCounter m_packetcounter;
	// Block mesh animation parameters
	float m_animation_time;
	std::atomic_int m_crack_level;
	v3s16 m_crack_pos;
	// 0 <= m_daynight_i < DAYNIGHT_CACHE_COUNT
	//s32 m_daynight_i;
	//u32 m_daynight_ratio;
	Queue<std::string> m_chat_queue; // todo: convert to std::queue

	// The authentication methods we can use to enter sudo mode (=change password)
	u32 m_sudo_auth_methods;

	// The seed returned by the server in TOCLIENT_INIT is stored here
	u64 m_map_seed;

	// Auth data
	std::string m_playername;
	std::string m_password;
	bool is_simple_singleplayer_game;
	// If set, this will be sent (and cleared) upon a TOCLIENT_ACCEPT_SUDO_MODE
	std::string m_new_password;
	// Usable by auth mechanisms.
	AuthMechanism m_chosen_auth_mech;
	void * m_auth_data;

	bool m_access_denied;
	bool m_access_denied_reconnect;
	std::string m_access_denied_reason;
	Queue<ClientEvent> m_client_event_queue;
	//std::queue<ClientEvent> m_client_event_queue;
	bool m_itemdef_received;
	bool m_nodedef_received;
	ClientMediaDownloader *m_media_downloader;

	// time_of_day speed approximation for old protocol
	bool m_time_of_day_set;
	float m_last_time_of_day_f;
	float m_time_of_day_update_timer;

	// An interval for generally sending object positions and stuff
	float m_recommended_send_interval;

	// Sounds
	float m_removed_sounds_check_timer;
	// Mapping from server sound ids to our sound ids
	std::map<s32, int> m_sounds_server_to_client;
	// And the other way!
	std::map<int, s32> m_sounds_client_to_server;
	// And relations to objects
	std::map<int, u16> m_sounds_to_objects;

	// Privileges
	std::set<std::string> m_privileges;

	// Detached inventories
	// key = name
	std::map<std::string, Inventory*> m_detached_inventories;
	double m_uptime;
	bool m_simple_singleplayer_mode;
	float m_timelapse_timer;
public:
	bool use_weather = false;
	unsigned int overload = 0;
	void sendDrawControl();
private:

	// Storage for mesh data for creating multiple instances of the same mesh
	StringMap m_mesh_data;

	// own state
	LocalClientState m_state;

	// Used for saving server map to disk client-side
	Database *m_localdb;
	IntervalLimiter m_localdb_save_interval;
	u16 m_cache_save_interval;
	Server *m_localserver;

	// TODO: Add callback to update these when g_settings changes
	bool m_cache_smooth_lighting;
	bool m_cache_enable_shaders;
	bool m_cache_use_tangent_vertices;

	DISABLE_CLASS_COPY(Client);
};

#endif // !CLIENT_HEADER
