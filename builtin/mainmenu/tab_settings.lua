--Minetest
--Copyright (C) 2013 sapier
--
--This program is free software; you can redistribute it and/or modify
--it under the terms of the GNU Lesser General Public License as published by
--the Free Software Foundation; either version 2.1 of the License, or
--(at your option) any later version.
--
--This program is distributed in the hope that it will be useful,
--but WITHOUT ANY WARRANTY; without even the implied warranty of
--MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
--GNU Lesser General Public License for more details.
--
--You should have received a copy of the GNU Lesser General Public License along
--with this program; if not, write to the Free Software Foundation, Inc.,
--51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

--------------------------------------------------------------------------------

local labels = {
	leaves = {
		fgettext("Opaque Leaves"),
		fgettext("Simple Leaves"),
		fgettext("Fancy Leaves")
	},
	node_highlighting = {
		fgettext("Node Outlining"),
		fgettext("Node Highlighting"),
		fgettext("None")
	},
	filters = {
		fgettext("No Filter"),
		fgettext("Bilinear Filter"),
		fgettext("Trilinear Filter")
	},
	mipmap = {
		fgettext("No Mipmap"),
		fgettext("Mipmap"),
		fgettext("Mipmap + Aniso. Filter")
	},
	antialiasing = {
		fgettext("None"),
		fgettext("2x"),
		fgettext("4x"),
		fgettext("8x")
	},
	shadow_levels = {
		fgettext("Disabled"),
		fgettext("Very Low"),
		fgettext("Low"),
		fgettext("Medium"),
		fgettext("High"),
		fgettext("Very High")
	}
}

local dd_options = {
	leaves = {
		table.concat(labels.leaves, ","),
		{"opaque", "simple", "fancy"}
	},
	node_highlighting = {
		table.concat(labels.node_highlighting, ","),
		{"box", "halo", "none"}
	},
	filters = {
		table.concat(labels.filters, ","),
		{"", "bilinear_filter", "trilinear_filter"}
	},
	mipmap = {
		table.concat(labels.mipmap, ","),
		{"", "mip_map", "anisotropic_filter"}
	},
	antialiasing = {
		table.concat(labels.antialiasing, ","),
		{"0", "2", "4", "8"}
	},
	shadow_levels = {
		table.concat(labels.shadow_levels, ","),
		{ "0", "1", "2", "3", "4", "5" }
	}
}

local getSettingIndex = {
	Leaves = function()
		local style = core.settings:get("leaves_style")
		for idx, name in pairs(dd_options.leaves[2]) do
			if style == name then return idx end
		end
		return 1
	end,
	NodeHighlighting = function()
		local style = core.settings:get("node_highlighting")
		for idx, name in pairs(dd_options.node_highlighting[2]) do
			if style == name then return idx end
		end
		return 1
	end,
	Filter = function()
		if core.settings:get(dd_options.filters[2][3]) == "true" then
			return 3
		elseif core.settings:get(dd_options.filters[2][3]) == "false" and
				core.settings:get(dd_options.filters[2][2]) == "true" then
			return 2
		end
		return 1
	end,
	Mipmap = function()
		if core.settings:get(dd_options.mipmap[2][3]) == "true" then
			return 3
		elseif core.settings:get(dd_options.mipmap[2][3]) == "false" and
				core.settings:get(dd_options.mipmap[2][2]) == "true" then
			return 2
		end
		return 1
	end,
	Antialiasing = function()
		local antialiasing_setting = core.settings:get("fsaa")
		for i = 1, #dd_options.antialiasing[2] do
			if antialiasing_setting == dd_options.antialiasing[2][i] then
				return i
			end
		end
		return 1
	end,
	ShadowMapping = function()
		local shadow_setting = core.settings:get("shadow_levels")
		for i = 1, #dd_options.shadow_levels[2] do
			if shadow_setting == dd_options.shadow_levels[2][i] then
				return i
			end
		end
		return 1
	end
}

local function antialiasing_fname_to_name(fname)
	for i = 1, #labels.antialiasing do
		if fname == labels.antialiasing[i] then
			return dd_options.antialiasing[2][i]
		end
	end
	return 0
end

local function formspec(tabview, name, tabdata)
	local tab_string =
		"box[0,0;3.75,4.5;#999999]" ..
		"checkbox[0.25,0;cb_smooth_lighting;" .. fgettext("Smooth Lighting") .. ";"
				.. dump(core.settings:get_bool("smooth_lighting")) .. "]" ..
		"checkbox[0.25,0.5;cb_particles;" .. fgettext("Particles") .. ";"
				.. dump(core.settings:get_bool("enable_particles")) .. "]" ..
		"checkbox[0.25,1;cb_3d_clouds;" .. fgettext("3D Clouds") .. ";"
				.. dump(core.settings:get_bool("enable_3d_clouds")) .. "]" ..
		"checkbox[0.25,1.5;cb_opaque_water;" .. fgettext("Opaque Water") .. ";"
				.. dump(core.settings:get_bool("opaque_water")) .. "]" ..
		"checkbox[0.25,2.0;cb_connected_glass;" .. fgettext("Connected Glass") .. ";"
				.. dump(core.settings:get_bool("connected_glass")) .. "]" ..
		"dropdown[0.25,2.8;3.5;dd_node_highlighting;" .. dd_options.node_highlighting[1] .. ";"
				.. getSettingIndex.NodeHighlighting() .. "]" ..
		"dropdown[0.25,3.6;3.5;dd_leaves_style;" .. dd_options.leaves[1] .. ";"
				.. getSettingIndex.Leaves() .. "]" ..
		"box[4,0;3.75,4.9;#999999]" ..
		"label[4.25,0.1;" .. fgettext("Texturing:") .. "]" ..
		"dropdown[4.25,0.55;3.5;dd_filters;" .. dd_options.filters[1] .. ";"
				.. getSettingIndex.Filter() .. "]" ..
		"dropdown[4.25,1.35;3.5;dd_mipmap;" .. dd_options.mipmap[1] .. ";"
				.. getSettingIndex.Mipmap() .. "]" ..
		"label[4.25,2.15;" .. fgettext("Antialiasing:") .. "]" ..
		"dropdown[4.25,2.6;3.5;dd_antialiasing;" .. dd_options.antialiasing[1] .. ";"
				.. getSettingIndex.Antialiasing() .. "]" ..
		"box[8,0;3.75,4.5;#999999]"

	local video_driver = core.settings:get("video_driver")
	local shaders_enabled = core.settings:get_bool("enable_shaders")
	if video_driver == "opengl" then
		tab_string = tab_string ..
			"checkbox[8.25,0;cb_shaders;" .. fgettext("Shaders") .. ";"
					.. tostring(shaders_enabled) .. "]"
	elseif video_driver == "ogles2" then
		tab_string = tab_string ..
			"checkbox[8.25,0;cb_shaders;" .. fgettext("Shaders (experimental)") .. ";"
					.. tostring(shaders_enabled) .. "]"
	else
		core.settings:set_bool("enable_shaders", false)
		shaders_enabled = false
		tab_string = tab_string ..
			"label[8.38,0.2;" .. core.colorize("#888888",
					fgettext("Shaders (unavailable)")) .. "]"
	end

	if PLATFORM ~= "Android" then
		tab_string = tab_string ..
		"button[8,4.75;3.95,1;btn_change_keys;"
		.. fgettext("Change Keys") .. "]"
	end

	tab_string = tab_string ..
		"button[0,4.75;3.95,1;btn_advanced_settings;"
		.. fgettext("All Settings") .. "]"


	if core.settings:get("touchscreen_threshold") ~= nil then
		tab_string = tab_string ..
			"label[4.25,3.5;" .. fgettext("Touch threshold (px):") .. "]" ..
			"dropdown[4.25,3.95;3.5;dd_touchthreshold;0,10,20,30,40,50;" ..
			((tonumber(core.settings:get("touchscreen_threshold")) / 10) + 1) ..
			"]"
	else
		tab_string = tab_string ..
			"label[4.25,3.65;" .. fgettext("Screen:") .. "]" ..
			"checkbox[4.25,3.9;cb_autosave_screensize;" .. fgettext("Autosave Screen Size") .. ";"
					.. dump(core.settings:get_bool("autosave_screensize")) .. "]"
	end

	if shaders_enabled then
		tab_string = tab_string ..
			"checkbox[8.25,0.5;cb_tonemapping;" .. fgettext("Tone Mapping") .. ";"
					.. dump(core.settings:get_bool("tone_mapping")) .. "]" ..
			"checkbox[8.25,1;cb_waving_water;" .. fgettext("Waving Liquids") .. ";"
					.. dump(core.settings:get_bool("enable_waving_water")) .. "]" ..
			"checkbox[8.25,1.5;cb_waving_leaves;" .. fgettext("Waving Leaves") .. ";"
					.. dump(core.settings:get_bool("enable_waving_leaves")) .. "]" ..
			"checkbox[8.25,2;cb_waving_plants;" .. fgettext("Waving Plants") .. ";"
					.. dump(core.settings:get_bool("enable_waving_plants")) .. "]"

		if video_driver == "opengl" then
			tab_string = tab_string ..
				"label[8.25,2.8;" .. fgettext("Dynamic shadows:") .. "]" ..
				"label[8.25,3.2;" .. fgettext("(game support required)") .. "]" ..
					"dropdown[8.25,3.7;3.5;dd_shadows;" .. dd_options.shadow_levels[1] .. ";"
					.. getSettingIndex.ShadowMapping() .. "]"
		else
			tab_string = tab_string ..
				"label[8.38,2.7;" .. core.colorize("#888888",
					fgettext("Dynamic shadows")) .. "]"
		end
	else
		tab_string = tab_string ..
			"label[8.38,0.7;" .. core.colorize("#888888",
					fgettext("Tone Mapping")) .. "]" ..
			"label[8.38,1.2;" .. core.colorize("#888888",
					fgettext("Waving Liquids")) .. "]" ..
			"label[8.38,1.7;" .. core.colorize("#888888",
					fgettext("Waving Leaves")) .. "]" ..
			"label[8.38,2.2;" .. core.colorize("#888888",
					fgettext("Waving Plants")) .. "]"..
			"label[8.38,2.7;" .. core.colorize("#888888",
					fgettext("Dynamic shadows")) .. "]"
	end

	return tab_string
end

--------------------------------------------------------------------------------
local function handle_settings_buttons(this, fields, tabname, tabdata)

	if fields["btn_advanced_settings"] ~= nil then
		local adv_settings_dlg = create_adv_settings_dlg()
		adv_settings_dlg:set_parent(this)
		this:hide()
		adv_settings_dlg:show()
		--mm_game_theme.update("singleplayer", current_game())
		return true
	end
	if fields["cb_smooth_lighting"] then
		core.settings:set("smooth_lighting", fields["cb_smooth_lighting"])
		return true
	end
	if fields["cb_particles"] then
		core.settings:set("enable_particles", fields["cb_particles"])
		return true
	end
	if fields["cb_3d_clouds"] then
		core.settings:set("enable_3d_clouds", fields["cb_3d_clouds"])
		return true
	end
	if fields["cb_opaque_water"] then
		core.settings:set("opaque_water", fields["cb_opaque_water"])
		return true
	end
	if fields["cb_connected_glass"] then
		core.settings:set("connected_glass", fields["cb_connected_glass"])
		return true
	end
	if fields["cb_autosave_screensize"] then
		core.settings:set("autosave_screensize", fields["cb_autosave_screensize"])
		return true
	end
	if fields["cb_shaders"] then
		core.settings:set("enable_shaders", fields["cb_shaders"])
		return true
	end
	if fields["cb_tonemapping"] then
		core.settings:set("tone_mapping", fields["cb_tonemapping"])
		return true
	end
	if fields["cb_waving_water"] then
		core.settings:set("enable_waving_water", fields["cb_waving_water"])
		return true
	end
	if fields["cb_waving_leaves"] then
		core.settings:set("enable_waving_leaves", fields["cb_waving_leaves"])
	end
	if fields["cb_waving_plants"] then
		core.settings:set("enable_waving_plants", fields["cb_waving_plants"])
		return true
	end
	if fields["btn_change_keys"] then
		core.show_keys_menu()
		return true
	end
	if fields["cb_touchscreen_target"] then
		core.settings:set("touchtarget", fields["cb_touchscreen_target"])
		return true
	end

	--Note dropdowns have to be handled LAST!
	local ddhandled = false

	for i = 1, #labels.leaves do
		if fields["dd_leaves_style"] == labels.leaves[i] then
			core.settings:set("leaves_style", dd_options.leaves[2][i])
			ddhandled = true
		end
	end
	for i = 1, #labels.node_highlighting do
		if fields["dd_node_highlighting"] == labels.node_highlighting[i] then
			core.settings:set("node_highlighting", dd_options.node_highlighting[2][i])
			ddhandled = true
		end
	end
	if fields["dd_filters"] == labels.filters[1] then
		core.settings:set("bilinear_filter", "false")
		core.settings:set("trilinear_filter", "false")
		ddhandled = true
	elseif fields["dd_filters"] == labels.filters[2] then
		core.settings:set("bilinear_filter", "true")
		core.settings:set("trilinear_filter", "false")
		ddhandled = true
	elseif fields["dd_filters"] == labels.filters[3] then
		core.settings:set("bilinear_filter", "false")
		core.settings:set("trilinear_filter", "true")
		ddhandled = true
	end
	if fields["dd_mipmap"] == labels.mipmap[1] then
		core.settings:set("mip_map", "false")
		core.settings:set("anisotropic_filter", "false")
		ddhandled = true
	elseif fields["dd_mipmap"] == labels.mipmap[2] then
		core.settings:set("mip_map", "true")
		core.settings:set("anisotropic_filter", "false")
		ddhandled = true
	elseif fields["dd_mipmap"] == labels.mipmap[3] then
		core.settings:set("mip_map", "true")
		core.settings:set("anisotropic_filter", "true")
		ddhandled = true
	end
	if fields["dd_antialiasing"] then
		core.settings:set("fsaa",
			antialiasing_fname_to_name(fields["dd_antialiasing"]))
		ddhandled = true
	end
	if fields["dd_touchthreshold"] then
		core.settings:set("touchscreen_threshold", fields["dd_touchthreshold"])
		ddhandled = true
	end

	for i = 1, #labels.shadow_levels do
		if fields["dd_shadows"] == labels.shadow_levels[i] then
			core.settings:set("shadow_levels", dd_options.shadow_levels[2][i])
			ddhandled = true
		end
	end

	if fields["dd_shadows"] == labels.shadow_levels[1] then
		core.settings:set("enable_dynamic_shadows", "false")
	else
		local shadow_presets = {
			[2] = { 62,  512,  "true", 0, "false" },
			[3] = { 93,  1024, "true", 0, "false" },
			[4] = { 140, 2048, "true", 1, "false" },
			[5] = { 210, 4096, "true", 2,  "true" },
			[6] = { 300, 8192, "true", 2,  "true" },
		}
		local s = shadow_presets[table.indexof(labels.shadow_levels, fields["dd_shadows"])]
		if s then
			core.settings:set("enable_dynamic_shadows", "true")
			core.settings:set("shadow_map_max_distance", s[1])
			core.settings:set("shadow_map_texture_size", s[2])
			core.settings:set("shadow_map_texture_32bit", s[3])
			core.settings:set("shadow_filters", s[4])
			core.settings:set("shadow_map_color", s[5])
		end
	end

	return ddhandled
end

return {
	name = "settings",
	caption = fgettext("Settings"),
	cbf_formspec = formspec,
	cbf_button_handler = handle_settings_buttons
}
