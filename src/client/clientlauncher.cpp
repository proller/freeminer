// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2010-2013 celeron55, Perttu Ahola <celeron55@gmail.com>

#include <functional>
#include "mainloop.h"
#include "../server/serverlist.h"
#include "gui/mainmenumanager.h"
#include "clouds.h"
#include "gui/touchcontrols.h"
#include "filesys.h"
#include "gui/guiMainMenu.h"
#include "game.h"
#include "player.h"
#include "chat.h"
#include "gettext.h"
#include "inputhandler.h"
#include "profiler.h"
#include "gui/guiEngine.h"
#include "fontengine.h"
#include "clientlauncher.h"
#include "version.h"
#include "renderingengine.h"
#include "settings.h"
#include "gettime.h"
#include "util/numeric.h"
#include "util/tracy_wrapper.h"
#include <IGUISpriteBank.h>
#include <ICameraSceneNode.h>
#include <unordered_map>

#if USE_SOUND
	#include "sound/sound_openal.h"
#endif

#include "debug.h"

/* mainmenumanager.h
 */
gui::IGUIEnvironment *guienv = nullptr;
gui::IGUIStaticText *guiroot = nullptr;
MainMenuManager g_menumgr;

// Passed to menus to allow disconnecting and exiting
MainGameCallback *g_gamecallback = nullptr;

#if 0
// This can be helpful for the next code cleanup
static void dump_start_data(const GameStartData &data)
{
	std::cout <<
		"\ndedicated   " << (int)data.is_dedicated_server <<
		"\nport        " << data.socket_port <<
		"\nworld_path  " << data.world_spec.path <<
		"\nworld game  " << data.world_spec.gameid <<
		"\ngame path   " << data.game_spec.path <<
		"\nplayer name " << data.name <<
		"\naddress     " << data.address << std::endl;
}
#endif

ClientLauncher::~ClientLauncher()
{
	delete input;

	g_settings->deregisterAllChangedCallbacks(this);

	delete g_fontengine;
	g_fontengine = nullptr;
	delete g_gamecallback;
	g_gamecallback = nullptr;

	guiroot = nullptr;
	guienv = nullptr;
	assert(g_menumgr.menuCount() == 0);

	delete m_rendering_engine;

	// delete event receiver only after all Irrlicht stuff is gone
	delete receiver;

#if USE_SOUND
	g_sound_manager_singleton.reset();
#endif
}

extern "C" {
	void preinit_sound(void);
}

void preinit_sound(void) {
#if USE_SOUND
	g_sound_manager_singleton = createSoundManagerSingleton();
#endif
}

//#ifdef __EMSCRIPTEN__
std::unique_ptr<IWritableShaderSource> g_clouds_ssrc;
//#endif

void ClientLauncher::run(std::function<void(bool)> resolve)
{
	/* This function is called when a client must be started.
	 * Covered cases:
	 *   - Singleplayer (address but map provided)
	 *   - Join server (no map but address provided)
	 *   - Local server (for main menu only)
	*/

	init_args(start_data, cmd_args);

#if !__EMSCRIPTEN__
#if USE_SOUND
	g_sound_manager_singleton = createSoundManagerSingleton();
#endif
#endif

	if (!init_engine()) {
		resolve(false); return;
	}

	if (!m_rendering_engine->get_video_driver()) {
		errorstream << "Could not initialize video driver." << std::endl;
		resolve(false); return;
	}

	m_rendering_engine->setupTopLevelWindow();

	//m_rendering_engine->getVideoDriver()->setMinHardwareBufferVertexCount(100);

	// Create game callback for menus
	g_gamecallback = new MainGameCallback();

	m_rendering_engine->setResizable(true);

	init_input();

	guienv = m_rendering_engine->get_gui_env();
	config_guienv();
	g_settings->registerChangedCallback("dpi_change_notifier", setting_changed_callback, this);
	g_settings->registerChangedCallback("display_density_factor", setting_changed_callback, this);
	g_settings->registerChangedCallback("gui_scaling", setting_changed_callback, this);

	g_fontengine = new FontEngine(guienv);

	// Create the menu clouds
	// This is only global so it can be used by RenderingEngine::draw_load_screen().
	assert(!g_menucloudsmgr && !g_menuclouds);
	g_clouds_ssrc.reset(createShaderSource());
	g_clouds_ssrc->addShaderUniformSetterFactory(std::make_unique<FogShaderUniformSetterFactory>());
	g_menucloudsmgr = m_rendering_engine->get_scene_manager()->createNewSceneManager();
	{
		struct tm tm = mt_localtime();
		u32 seed = (tm.tm_year << 16) | tm.tm_yday; // unique clouds every day
		g_menuclouds = new Clouds(g_menucloudsmgr, g_clouds_ssrc.get(), -1, seed);
	}
	g_menuclouds->setHeight(100.0f);
	g_menuclouds->update(v3f(0, 0, 0), m_rendering_engine->m_menu_clouds_color);
	scene::ICameraSceneNode* camera;
	camera = g_menucloudsmgr->addCameraSceneNode(NULL, v3f(0, 0, 0), v3f(0, 120, 100));
	camera->setFarValue(10000);

#ifdef __ANDROID__
	wait_data();
#endif

	/*
		GUI stuff
	*/

	chat_backend = new ChatBackend();

	// If an error occurs, this is set to something by menu().
	// It is then displayed before the menu shows on the next call to menu()
	error_message = "";
	reconnect_requested = false;

	first_loop = true;

	/*
		Menu-game loop
	*/
	retval = true;
	kill = porting::signal_handler_killstatus();

	// HEREHERE
	MainLoop::NextFrame([this, resolve]() { run_loop(resolve); });
}

void ClientLauncher::run_loop(std::function<void(bool)> resolve) {
	// EXTRANEOUS INDENT
		bool keep_running = m_rendering_engine->run() && !*kill && !g_gamecallback->shutdown_requested;
		if (!keep_running) {
			run_cleanup(resolve);
			return;
		}

		// Set the window caption
		auto driver_name = m_rendering_engine->getVideoDriver()->getName();
		std::string caption = std::string(PROJECT_NAME_C) +
			" " + g_version_hash +
			" [" + gettext("Main Menu") + "]" +
			" [" + driver_name + "]";

		m_rendering_engine->get_raw_device()->
			setWindowCaption(utf8_to_wide(caption).c_str());

		// EXTRA INDENT
		m_rendering_engine->get_gui_env()->clear();

		/*
			We need some kind of a root node to be able to add
			custom gui elements directly on the screen.
			Otherwise they won't be automatically drawn.
		*/
		guiroot = m_rendering_engine->get_gui_env()->addStaticText(L"",
			core::rect<s32>(0, 0, 10000, 10000));

		launch_game([this, resolve](bool should_run_game) { run_after_launch_game(resolve, should_run_game); });
}

void ClientLauncher::run_after_launch_game(std::function<void(bool)> resolve, bool should_run_game) {

	// EXTRANEOUS INDENT
			// Reset the reconnect_requested flag
			reconnect_requested = false;

			// If skip_main_menu, we only want to startup once
			if (skip_main_menu && !first_loop) {
				run_cleanup(resolve);
				return;
			}

			first_loop = false;

			if (!should_run_game) {
				if (skip_main_menu) {
					run_cleanup(resolve);
					return;
				}

				MainLoop::NextFrame([this, resolve]() { run_loop(resolve); });
				return;
			}

			// Break out of menu-game loop to shut down cleanly
			if (!m_rendering_engine->run() || *kill) {
				run_cleanup(resolve);
				return;
			}

			int tries = start_data.isSinglePlayer() ? 1 : g_settings->getU16("reconnects");
			int n = 0;

			while(!*kill && ++n <= tries &&
			the_game(
				kill,
				input,
				m_rendering_engine,
				&start_data,
				error_message,
				chat_backend,
				&reconnect_requested,
				autoexit,
				[this, resolve]() { after_the_game(resolve); }
			)
			){
				m_rendering_engine->get_scene_manager()->clear();
				errorstream << "Reconnecting "<< n << "/" << tries << " ..." << '\n';
			}
}

void ClientLauncher::after_the_game(std::function<void(bool)> resolve) {
	// EXTRANEOUS INDENT
					// AFTER TRY
					m_rendering_engine->get_scene_manager()->clear();

		if (g_touchcontrols) {
			delete g_touchcontrols;
			g_touchcontrols = NULL;
		}

		/* Save the settings when leaving the game.
		 * This makes sure that setting changes made in-game are persisted even
		 * in case of a later unclean exit from the mainmenu.
		 * This is especially useful on Android because closing the app from the
		 * "Recents screen" results in an unclean exit.
		 * Caveat: This means that the settings are saved twice when exiting Minetest.
		 */
		if (!g_settings_path.empty())
			g_settings->updateConfigFile(g_settings_path.c_str());

					// If no main menu, show error and exit
					if (skip_main_menu) {
						if (!error_message.empty())
							retval = false;
						run_cleanup(resolve);
						return;
					}
					MainLoop::NextFrame([this, resolve]() { run_loop(resolve); });
					return;
}

void ClientLauncher::run_cleanup(std::function<void(bool)> resolve) {
	// If profiler was enabled print it one last time
	if (g_settings->getFloat("profiler_print_interval") > 0) {
		infostream << "Profiler:" << std::endl;
		g_profiler->print(infostream);
		g_profiler->clear();
	}

	assert(g_menucloudsmgr->getReferenceCount() == 1);
	g_menucloudsmgr->drop();
	g_menucloudsmgr = nullptr;
	assert(g_menuclouds->getReferenceCount() == 1);
	g_menuclouds->drop();
	g_menuclouds = nullptr;
	g_clouds_ssrc.reset();
	resolve(retval);
	return;
}

void ClientLauncher::init_args(GameStartData &start_data, const Settings &cmd_args)
{
	skip_main_menu = cmd_args.getFlag("go");

	start_data.address = g_settings->get("address");
	if (cmd_args.exists("address")) {
		// Join a remote server
		start_data.address = cmd_args.get("address");
		start_data.world_path.clear();
		start_data.name = g_settings->get("name");
	}
	if (!start_data.world_path.empty()) {
		// Start a singleplayer instance
		start_data.address = "";
	}

	if (cmd_args.exists("name"))
		start_data.name = cmd_args.get("name");

	// If a world was commanded, select it
	if (!start_data.world_path.empty()) {
		auto &spec = start_data.world_spec;

		spec.path = start_data.world_path;
		spec.gameid = getWorldGameId(spec.path, true);
		spec.name = _("[--world parameter]");
	}

	random_input = g_settings->getBool("random_input")
			|| cmd_args.getFlag("random-input");

	int autoexit_ = 0;
	cmd_args.getS32NoEx("autoexit", autoexit_);
	autoexit = autoexit_;
}

bool ClientLauncher::init_engine()
{
	receiver = new MyEventReceiver();
	try {
		m_rendering_engine = new RenderingEngine(receiver);
	} catch (std::exception &e) {
		errorstream << e.what() << std::endl;
	}
	return !!m_rendering_engine;
}

void ClientLauncher::init_input()
{
	if (random_input)
		input = new RandomInputHandler();
	else
		input = new RealInputHandler(receiver);

	if (g_settings->getBool("enable_joysticks"))
		init_joysticks();
}

void ClientLauncher::init_joysticks()
{
	core::array<SJoystickInfo> infos;
	std::vector<SJoystickInfo> joystick_infos;

	// Make sure this is called maximum once per
	// irrlicht device, otherwise it will give you
	// multiple events for the same joystick.
	if (!m_rendering_engine->get_raw_device()->activateJoysticks(infos)) {
		errorstream << "Could not activate joystick support." << std::endl;
		return;
	}

	infostream << "Joystick support enabled" << std::endl;
	joystick_infos.reserve(infos.size());
	for (u32 i = 0; i < infos.size(); i++) {
		joystick_infos.push_back(infos[i]);
	}
	input->joystick.onJoystickConnect(joystick_infos);
}

void ClientLauncher::setting_changed_callback(const std::string &name, void *data)
{
	static_cast<ClientLauncher*>(data)->config_guienv();
}

static video::ITexture *loadTexture(video::IVideoDriver *driver, const char *path)
{
	// FIXME?: it would be cleaner to do this through a ITextureSource, but we don't have one
	video::ITexture *texture = nullptr;
	verbosestream << "Loading texture " << path << std::endl;
	if (auto *image = driver->createImageFromFile(path); image) {
		texture = driver->addTexture(fs::GetFilenameFromPath(path), image);
		image->drop();
	}
	return texture;
}

void ClientLauncher::config_guienv()
{
	gui::IGUISkin *skin = guienv->getSkin();

	skin->setColor(gui::EGDC_WINDOW_SYMBOL, video::SColor(255, 255, 255, 255));
	skin->setColor(gui::EGDC_BUTTON_TEXT, video::SColor(255, 255, 255, 255));
	skin->setColor(gui::EGDC_3D_LIGHT, video::SColor(0, 0, 0, 0));
	skin->setColor(gui::EGDC_3D_HIGH_LIGHT, video::SColor(255, 30, 30, 30));
	skin->setColor(gui::EGDC_3D_SHADOW, video::SColor(255, 0, 0, 0));
	//skin->setColor(gui::EGDC_HIGH_LIGHT, video::SColor(255, 70, 120, 50));
	skin->setColor(gui::EGDC_HIGH_LIGHT, video::SColor(255, 56, 121, 65));
	skin->setColor(gui::EGDC_HIGH_LIGHT_TEXT, video::SColor(255, 255, 255, 255));
	skin->setColor(gui::EGDC_EDITABLE, video::SColor(255, 128, 128, 128));
	//skin->setColor(gui::EGDC_FOCUSED_EDITABLE, video::SColor(255, 96, 134, 49));
	skin->setColor(gui::EGDC_FOCUSED_EDITABLE, video::SColor(255, 97, 173, 109));

	float density = rangelim(g_settings->getFloat("gui_scaling"), 0.5f, 20) *
		RenderingEngine::getDisplayDensity();
	skin->setScale(density);
	skin->setSize(gui::EGDS_CHECK_BOX_WIDTH, (s32)(17.0f * density));
	skin->setSize(gui::EGDS_SCROLLBAR_SIZE, (s32)(21.0f * density));
	skin->setSize(gui::EGDS_WINDOW_BUTTON_WIDTH, (s32)(15.0f * density));

	static u32 orig_sprite_id = skin->getIcon(gui::EGDI_CHECK_BOX_CHECKED);
	static std::unordered_map<std::string, u32> sprite_ids;

	if (density > 1.5f) {
		// Texture dimensions should be a power of 2
		std::string path = porting::path_share + "/textures/base/pack/";
		if (density > 3.5f)
			path.append("checkbox_64.png");
		else if (density > 2.0f)
			path.append("checkbox_32.png");
		else
			path.append("checkbox_16.png");

		auto cached_id = sprite_ids.find(path);
		if (cached_id != sprite_ids.end()) {
			skin->setIcon(gui::EGDI_CHECK_BOX_CHECKED, cached_id->second);
		} else {
			auto *driver = m_rendering_engine->get_video_driver();
			auto *texture = loadTexture(driver, path.c_str());
			s32 id = skin->getSpriteBank()->addTextureAsSprite(texture);
			if (id != -1) {
				skin->setIcon(gui::EGDI_CHECK_BOX_CHECKED, id);
				sprite_ids.emplace(path, id);
			}
		}
	} else {
		skin->setIcon(gui::EGDI_CHECK_BOX_CHECKED, orig_sprite_id);
	}
}

bool ClientLauncher::launch_game(std::function<void(bool)> resolve)
/*
bool ClientLauncher::launch_game(std::string &error_message,
		bool reconnect_requested, GameStartData &start_data,
		const Settings &cmd_args)
*/
{
	// Prepare and check the start data to launch a game
	std::string error_message_lua = error_message;
	error_message.clear();

	if (cmd_args.exists("password"))
		start_data.password = cmd_args.get("password");

	if (cmd_args.exists("password-file")) {
		std::ifstream passfile(cmd_args.get("password-file"));
		if (passfile.good()) {
			std::getline(passfile, start_data.password);
		} else {
			error_message = gettext("Provided password file "
					"failed to open: ")
					+ cmd_args.get("password-file");
			errorstream << error_message << std::endl;
			resolve(false); return false;
		}
	}

	/*
	 * Show the GUI menu
	 */
	server_name = "";
	server_description = "";
	if (!skip_main_menu) {
		// Initialize menu data
		// TODO: Re-use existing structs (GameStartData)

		menudata_addr = new MainMenuData();
		MainMenuData &menudata = *menudata_addr;
		menudata.address                         = start_data.address;
		menudata.name                            = start_data.name;
		menudata.password                        = start_data.password;
		menudata.port                            = itos(start_data.socket_port);
		menudata.script_data.errormessage        = std::move(error_message_lua);
		menudata.script_data.reconnect_requested = reconnect_requested;

		main_menu([this, resolve]() { after_main_menu(resolve); });
	} else {
		after_main_menu(resolve);
	}
	return true;
}

void ClientLauncher::after_main_menu(std::function<void(bool)> resolve) {
	if (!skip_main_menu) {
		MainMenuData &menudata = *menudata_addr;

		// Skip further loading if there was an exit signal.
		if (!m_rendering_engine->run() || *porting::signal_handler_killstatus())
	       {
		    delete menudata_addr; menudata_addr = nullptr;
			resolve(false); return;
			//return false;
		   }

		if (!menudata.script_data.errormessage.empty()) {
			/* The calling function will pass this back into this function upon the
			 * next iteration (if any) causing it to be displayed by the GUI
			 */
			error_message = menudata.script_data.errormessage;
			delete menudata_addr; menudata_addr = nullptr;
			resolve(false); return;
		}

		int newport = stoi(menudata.port);
		if (newport != 0)
			start_data.socket_port = newport;

		// Update world information using main menu data
		std::vector<WorldSpec> worldspecs = getAvailableWorlds();

		int world_index = menudata.selected_world;
		if (world_index >= 0 && world_index < (int)worldspecs.size()) {
			start_data.world_spec = worldspecs[world_index];
			start_data.world_path = start_data.world_spec.path;
		}

		start_data.name = menudata.name;
		start_data.password = menudata.password;
		start_data.address = std::move(menudata.address);
		start_data.allow_login_or_register = menudata.allow_login_or_register;
		server_name = menudata.servername;
		server_description = menudata.serverdescription;

		start_data.local_server = !menudata.simple_singleplayer_mode &&
			start_data.address.empty();
		delete menudata_addr;
		menudata_addr = nullptr;
	} else {
		start_data.local_server = !start_data.world_path.empty() &&
			start_data.address.empty() && !start_data.name.empty();
	}

	if (!start_data.isSinglePlayer() && start_data.name.empty()) {
		error_message = gettext("Please choose a name!");
		errorstream << error_message << std::endl;
		error_message.clear();

		// fm:
		auto num_add = 0;
#if __EMSCRIPTEN__
		num_add = 1000000;
#endif
       	start_data.name = std::string("Guest") + itos(myrand_range(num_add + 100000, num_add + 999999));
		// ===
	}

	// If using simple singleplayer mode, override
	if (start_data.isSinglePlayer()) {
		start_data.name = "singleplayer";
		start_data.password = "";
		start_data.socket_port = myrand_range(49152, 65535);

#if USE_MULTI
		start_data.socket_port -= 200; //max diffport
#endif

	} else {
		g_settings->set("name", start_data.name);
	}

	if (start_data.name.length() > PLAYERNAME_SIZE - 1) {
		error_message = gettext("Player name too long.");
		start_data.name.resize(PLAYERNAME_SIZE);
		g_settings->set("name", start_data.name);
		resolve(false); return;
	}

	// For singleplayer and local server
	if (start_data.address.empty()) {
		auto &worldspec = start_data.world_spec;
		if (worldspec.path.empty()) {
			error_message = _("No world selected and no address "
					"provided. Nothing to do.");
			errorstream << error_message << std::endl;
			resolve(false); return;
		}

		infostream << "Selected world: " << worldspec.name
			<< " [" << worldspec.path << "]" << std::endl;

		// Figure out which game we'll be using
		// Note that start_data.game_spec contains the gameid from the command line
		bool world_exists = getWorldExists(worldspec.path);

		if (!world_exists) {
			try {
				loadGameConfAndInitWorld(worldspec.path,
						fs::GetFilenameFromPath(worldspec.path.c_str()),
						start_data.game_spec, true);
				world_exists = getWorldExists(worldspec.path);
			} catch (const std::exception &e) {
				errorstream << "Create world error: " << e.what() << std::endl;
			}
		}

		if (world_exists) {
			auto world_game = findWorldSubgame(worldspec.path);
			if (world_game.isValid())
				start_data.game_spec = world_game;
		}

		if (!start_data.game_spec.isValid()) {
			if (world_exists) {
				error_message = gettext("Could not find or load game: ")
					+ worldspec.gameid;
			} else {
				error_message = gettext("World does not exist and no game selected to create one.");
			}
			errorstream << error_message << std::endl;
			resolve(false); return;
		}
	}

	resolve(true); return;
}

void ClientLauncher::main_menu(std::function<void()> resolve)
{
	ServerList::lan_get();

	kill   = porting::signal_handler_killstatus();

	// Wait until app is in foreground because of #15883
	infostream << "Waiting for app to be in foreground" << std::endl;
	main_menu_wait_loop(resolve);
}

void ClientLauncher::main_menu_wait_loop(std::function<void()> resolve)
{
	auto device = m_rendering_engine->get_raw_device();
	bool keep_going = m_rendering_engine->run() && !*kill;
	if (keep_going && !device->isWindowVisible()) {
		MainLoop::NextFrame([this, resolve]() { main_menu_wait_loop(resolve); });
		return;
	}

	infostream << "Waited for app to be in foreground" << std::endl;

	infostream << "Waiting for other menus" << std::endl;
	framemarker = new FrameMarker("ClientLauncher::main_menu()-wait-frame");
	main_menu_loop(resolve);
}

void ClientLauncher::main_menu_loop(std::function<void()> resolve) {
	// EXTRANEOUS INDENT
		bool keep_going = m_rendering_engine->run() && !*kill;
		if (!keep_going || !isMenuActive()) {
			main_menu_after_loop(resolve);
			return;
		}
		framemarker->start();
		video::IVideoDriver *driver = m_rendering_engine->get_video_driver();
		driver->beginScene(true, true, video::SColor(255, 128, 128, 128));
		m_rendering_engine->get_gui_env()->drawAll();
		driver->endScene();
		framemarker->end();
		// On some computers framerate doesn't seem to be automatically limited
		//sleep_ms(25);
     	MainLoop::NextFrame([this, resolve]() { main_menu_loop(resolve); });
}

void ClientLauncher::main_menu_after_loop(std::function<void()> resolve) {
	delete framemarker;
	framemarker = nullptr;
	infostream << "Waited for other menus" << std::endl;

	auto device = m_rendering_engine->get_raw_device();
	auto *cur_control = device->getCursorControl();
	if (cur_control) {
		// Cursor can be non-visible when coming from the game
		cur_control->setVisible(true);
		// Set absolute mouse mode
		cur_control->setRelativeMode(false);
	}

	/* show main menu */
	new GUIEngine(&input->joystick, guiroot, m_rendering_engine, &g_menumgr, menudata_addr, *kill, [this, resolve]() {
		main_menu_after_guiengine(resolve);
        });
	std::cout << "AFTER CONSTRUCTING GUIEngine" << std::endl;
}

void ClientLauncher::main_menu_after_guiengine(std::function<void()> resolve) {
	/* leave scene manager in a clean state */
	m_rendering_engine->get_scene_manager()->clear();

	ServerList::lan_adv_client.stop();

	/* Save the settings when leaving the mainmenu.
	 * This makes sure that setting changes made in the mainmenu are persisted
	 * even in case of a later unclean exit from the game.
	 * This is especially useful on Android because closing the app from the
	 * "Recents screen" results in an unclean exit.
	 * Caveat: This means that the settings are saved twice when exiting Minetest.
	 */
	if (!g_settings_path.empty())
		g_settings->updateConfigFile(g_settings_path.c_str());
	resolve();
}

//freeminer:
void ClientLauncher::wait_data() {
	const auto device = m_rendering_engine->get_raw_device();
	device->run();
	bool wait = false;
	std::vector<std::string> check_path { porting::path_share + DIR_DELIM + "builtin" + DIR_DELIM + "init.lua", g_settings->get("font_path") };
	for (const auto & p : check_path)
		if (!fs::PathExists(p)) {
			wait = true;
			break;
		}
	auto &kill = *porting::signal_handler_killstatus();
	for (int i = 0; i < 100; ++i) { // 10s max
		if (i || wait) {
			auto driver = device->getVideoDriver();
			g_menuclouds->step(4);
			driver->beginScene(true, true, video::SColor(255, 140, 186, 250));
			g_menucloudsmgr->drawAll();
			guienv->drawAll();
			driver->endScene();
			device->run();
			device->sleep(100);
		}
		int no = 0;
		if (! (i % 10) ) { //every second
			for (const auto & p : check_path)
				if (!fs::PathExists(p)) {
					++no;
					break;
				}
			if (!no || kill || !device->run())
				break;
			infostream << "waiting assets i= " << i << " path="<< porting::path_share << std::endl;
		}
	}

	if (wait) {
		device->run();
		device->sleep(300);
	}
}
