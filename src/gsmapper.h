/*
 * gsmapper.h
 * 
 * Copyright 2014 gsmanners <gsmanners@gmail.com>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 * 
 * 
 */

#ifndef GSMAPPER_HEADER
#define GSMAPPER_HEADER

#include "irrlichttypes_extrabloated.h"
#include "client.h"
#include <map>
#include <string>
#include <vector>

class gsMapper
{
	private:
		video::ITexture* d_texture;
		video::ITexture* d_pmarker;
		std::map<v3s16, v3s16> m_minimap;
		std::vector<video::SColor> d_colorids;
		std::map<u16, u16> d_nodeids;
		v3s16 d_origin;
		u16 d_posx;
		u16 d_posy;
		u16 d_width;
		u16 d_height;
		f32 d_scale;
		u32 d_alpha;
		u16 d_scan;
		s16 d_surface;
		u16 d_border;
		s16 d_scanX;
		s16 d_scanZ;
		s16 d_cooldown2;
		u16 d_texindex;
		u16 d_colordefs;
		bool d_above;
		bool d_tracking;
		bool d_valid;
		bool d_hastex;
		bool d_hasptex;
		bool m_radar;
		u16 m_zoom;
		u16 m_mode;
		bool m_scancomplete;
		bool m_scanstarted;
		v3s16 m_pos;

	public:
		IrrlichtDevice *device;
		Client *client;
		gsMapper(IrrlichtDevice *device, Client *client);
		video::IVideoDriver *driver;
		LocalPlayer *player;
		ITextureSource *tsrc;
		~gsMapper();
		video::SColor getColorFromId(u16 id);
		void setMapVis(u16 x, u16 y, u16 w, u16 h, f32 scale, u32 alpha, video::SColor back);
		u16 getMinimapMode();
		void setMinimapMode(u16 mode);
		void drawMap(v3s16 pos, ClientMap *map);
};

#endif
