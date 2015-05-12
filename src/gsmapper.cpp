/*
 * gsmapper.cpp
 * 
 * Copyright 2014 gsmanners <gsmanners@gmail.com>
 *           2015 RealBadAngel <maciej.kasatkin@o2.pl>  				
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

#include "logoutputbuffer.h"
#include "gsmapper.h"
#include "clientmap.h"
#include "nodedef.h"
#include "util/numeric.h"
#include "util/string.h"
#include <math.h>

gsMapper::gsMapper(IrrlichtDevice *device, Client *client)
{
	this->device	= device;
	this->client	= client;
	this->driver	= device->getVideoDriver();
	this->tsrc		= client->getTextureSource();
	this->player	= client->getEnv().getLocalPlayer();

	d_colorids.clear();
	d_nodeids.clear();

	d_colorids.push_back(video::SColor(0,0,0,0));		// 0 = background (air)
	d_nodeids[CONTENT_AIR] = 0;
	d_colorids.push_back(video::SColor(0,0,0,0));		// 1 = unloaded/error node
	d_nodeids[CONTENT_IGNORE] = 1;
	d_colorids.push_back(video::SColor(0,99,99,99));	// 2 = mystery node
	d_nodeids[CONTENT_UNKNOWN] = 2;
	d_colordefs = 3;

	d_valid = false;
	d_hastex = false;
	d_hasptex = false;
	m_minimap.clear();
}

gsMapper::~gsMapper()
{
	if (d_hastex)
	{
		driver->removeTexture(d_texture);
		d_hastex = false;
	}
	if (d_hasptex)
	{
		driver->removeTexture(d_pmarker);
		d_hasptex = false;
	}
}

/*
 * Convert the content id to a color value.
 *
 * Any time we encounter a node that isn't in the ids table, this converts the
 * texture of the node into a color. Otherwise, this returns what we already converted.
 */

video::SColor gsMapper::getColorFromId(u16 id)
{
	// check if already in my defs
	std::map<u16, u16>::iterator i = d_nodeids.find(id);
	if (i != d_nodeids.end()) {
		return d_colorids[d_nodeids[id]];

	} else {

		// get the tile image
		const ContentFeatures &f = client->getNodeDefManager()->get(id);
		video::ITexture *t = tsrc->getTexture(f.tiledef[0].name);
		assert(t);

		video::IImage *image = driver->createImage(t, 
			core::position2d<s32>(0, 0),
			t->getOriginalSize());
		assert(image);

		// copy to a 1-pixel image
		video::IImage *image2 = driver->createImage(video::ECF_A8R8G8B8,
			core::dimension2d<u32>(1, 1) );
		assert(image2);
		image->copyToScalingBoxFilter(image2, 0, false);
		image->drop();

		// add the color to my list
		d_colorids.push_back(image2->getPixel(0,0));
		image2->drop();
		d_nodeids[id] = d_colordefs;
		assert(d_colordefs < MAX_REGISTERED_CONTENT);
		return d_colorids[d_colordefs++];
	}
}

/*
 * Set the visual elements of the HUD (rectangle) on the display.
 *
 * x, y = screen coordinates of the HUD
 * w, h = width and height of the HUD
 * scale = pixel scale (pixels per node)
 * alpha = alpha component of the HUD
 * back = background color
 * 
 * Note that the scaling is not currently interpolated.
 */

void gsMapper::setMapVis(u16 x, u16 y, u16 w, u16 h, f32 scale, u32 alpha, video::SColor back)
{
	if (x != d_posx)
	{
		d_posx = x;
		d_valid = false;
	}
	if (y != d_posy)
	{
		d_posy = y;
		d_valid = false;
	}
	if (w != d_width)
	{
		d_width = w;
		d_valid = false;
	}
	if (h != d_height)
	{
		d_height = h;
		d_valid = false;
	}
	if (scale != d_scale)
	{
		d_scale = scale;
		d_valid = false;
	}
	if (alpha != d_alpha)
	{
		d_alpha = alpha;
		d_valid = false;
	}
	if (back != d_colorids[0])
	{
		d_colorids[0] = back;
		d_valid = false;
	}
}

/*
 * Set the parameters for map drawing.
 *
 * bAbove = only draw surface nodes (i.e. highest non-air nodes)
 * iScan = the number of nodes to scan in each column of the map
 * iSurface = the value of the Y position on the map considered the surface
 * bTracking = stay centered on position
 * iBorder = distance from map border where moved draw is triggered
 *
 * If bAbove is true, then draw will only consider surface nodes. If false, then
 * draw will include consideration of "wall" nodes.
 * 
 * If bTracking is false, then draw only moves the map when the position has
 * entered into a border region.
 */

void gsMapper::setMapType(bool bAbove, u16 iScan, s16 iSurface, bool bTracking, u16 iBorder)
{
	d_above = bAbove;
	d_scan = iScan;
	d_surface = iSurface;
	d_tracking = bTracking;
	d_border = iBorder;
}

/*
 * Perform the actual map draw.
 *
 * position = the focus point of the map
 *
 * This draws the map in increments over a number of calls, accumulating data 
 * with each iteration.
 */

void gsMapper::drawMap(v3s16 pos, ClientMap *map, bool radar, u16 zoom)
{
	m_minimap.clear();

	s16 scan_height = 128;
	if (radar) {
		scan_height = 16;
	}	

	s16 nwidth = floor(d_width / zoom);
	s16 nheight = floor(d_height / zoom);

	v3s16 origin (floor(pos.X - (nwidth / 2)), floor(pos.Y), floor(pos.Z - (nheight / 2)));

	v3s16 p;
	s16 x = 0;
	s16 y = 0;
	s16 z = 0;

	while (z < nheight)
	{
		p.Z = origin.Z + z;
		x = 0;
		while (x < nwidth)
		{
			p.X = origin.X + x;

			// surface scanner
			if (!radar)
			{
				p.Y = origin.Y + (scan_height / 2);
				for (y = 0; y < scan_height; y++)
				{
					MapNode n = map->getNodeNoEx(p);
					p.Y--;
					if (n.param0 != CONTENT_IGNORE && n.param0 != CONTENT_AIR)
						{
							p.Y = 0;
							m_minimap[p].X = n.param0;
							m_minimap[p].Y = y;
						break;
						}
				}

			// radar mode
			} else {
				p.Y = origin.Y - (scan_height / 2);
				for (y = 0; y < scan_height; y++)
				{
					MapNode n = map->getNodeNoEx(v3s16(p.X, p.Y + y, p.Z));
					if (n.param0 == CONTENT_AIR) {
						m_minimap[v3s16(p.X, 0, p.Z)].Y += 1;
					}
				}	
			}
			x++;
		}
		z++;
	}

	if (1)
	{
		// set up the image
		core::dimension2d<u32> dim(nwidth, nheight);
		video::IImage *image = driver->createImage(video::ECF_A8R8G8B8, dim);
		assert(image);

		for (z = 0; z < nheight; z++)
		{
			for (x = 0; x < nwidth; x++)
			{
				v3s16 p(origin.X + x, 0, origin.Z + z);

				u16 i = CONTENT_IGNORE;
				float factor = 0; 

				std::map<v3s16, v3s16>::iterator iter = m_minimap.find(p);
				if (iter != m_minimap.end()) {
					i = m_minimap[p].X;
					factor = m_minimap[p].Y;
				}

				video::SColor c(240,0,0,0);
				if (!radar) {
					c = getColorFromId(i);
					factor = 1.0 + 2.0*((scan_height/2)-factor)/scan_height; 
					c.setRed(core::clamp(core::round32(c.getRed() * factor), 0, 255));
					c.setGreen(core::clamp(core::round32(c.getGreen() * factor), 0, 255));
					c.setBlue(core::clamp(core::round32(c.getBlue() * factor), 0, 255));
				} else {
					if (factor > 0) {
						c.setGreen(core::clamp(core::round32(32 + factor * 8), 0, 255));
					}	
				}

				c.setAlpha(240);
				image->setPixel(x, nheight - z - 1, c);
			}
		}

	if (d_hastex)
	{
		driver->removeTexture(d_texture);
		d_hastex = false;
	}
		std::string f = "gsmapper__" + itos(device->getTimer()->getRealTime());
		d_texture = driver->addTexture(f.c_str(), image);
		assert(d_texture);
		d_hastex = true;
		image->drop();
	} 

	// draw map texture
	if (d_hastex) {
		driver->draw2DImage( d_texture,
			core::rect<s32>(d_posx, d_posy, d_posx+d_width, d_posy+d_height),
			core::rect<s32>(0, 0, nwidth, nheight),
			0, 0, true );
	}

	// draw local player marker
	if (tsrc->isKnownSourceImage("player_marker0.png"))
	{
		v3s16 p = floatToInt(player->getPosition(), BS);
		if ( p.X >= origin.X && p.X <= (origin.X + nwidth) &&
			p.Z >= origin.Z && p.Z <= (origin.Z + nheight) )
		{
			f32 y = floor(360 - wrapDegrees_0_360(player->getYaw()));
			if (y < 0) y = 0;
			int r = floor(y / 90.0);
			int a = floor(fmod(y, 90.0) / 10.0);
			std::string f = "player_marker" + itos(a) + ".png";
			video::ITexture *pmarker = tsrc->getTexture(f);
			assert(pmarker);
			v2u32 size = pmarker->getOriginalSize();

			if (r > 0)
			{
				core::dimension2d<u32> dim(size.X, size.Y);
				video::IImage *image1 = driver->createImage(pmarker,
					core::position2d<s32>(0, 0), dim);
				assert(image1);

				// rotate image
				video::IImage *image2 = driver->createImage(video::ECF_A8R8G8B8, dim);
				assert(image2);
				for (u32 y = 0; y < size.Y; y++)
				for (u32 x = 0; x < size.X; x++)
				{
					video::SColor c;
					if (r == 1)
					{
						c = image1->getPixel(y, (size.X - x - 1));
					} else if (r == 2) {
						c = image1->getPixel((size.X - x - 1), (size.Y - y - 1));
					} else {
						c = image1->getPixel((size.Y - y - 1), x);
					}
					image2->setPixel(x, y, c);
				}
				image1->drop();
				if (d_hasptex)
				{
					driver->removeTexture(d_pmarker);
					d_hasptex = false;
				}
				d_pmarker = driver->addTexture("playermarker__temp__", image2);
				assert(d_pmarker);
				pmarker = d_pmarker;
				d_hasptex = true;
				image2->drop();
			}

			s32 sx = d_posx + floor((p.X - origin.X) * zoom) - (size.X / 2);
			s32 sy = d_posy + floor((nheight - (p.Z - origin.Z)) * zoom) - (size.Y / 2);
			driver->draw2DImage( pmarker,
				core::rect<s32>(sx, sy, sx+size.X, sy+size.Y),
				core::rect<s32>(0, 0, size.X, size.Y),
				0, 0, true );
		}
	}
}
