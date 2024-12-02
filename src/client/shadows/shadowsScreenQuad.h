// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2021 Liso <anlismon@gmail.com>

#pragma once
#include "irrlichttypes_extrabloated.h"
#include "irr_ptr.h"
#include <IMaterialRendererServices.h>
#include <IShaderConstantSetCallBack.h>
#include "client/shader.h"

class shadowScreenQuad
{
public:
	shadowScreenQuad();

	void render(video::IVideoDriver *driver);
	video::SMaterial &getMaterial() { return m_meshbuffer->getMaterial(); }

private:
	irr_ptr<scene::SMeshBuffer> m_meshbuffer;
};

class shadowScreenQuadCB : public video::IShaderConstantSetCallBack
{
public:
	virtual void OnSetConstants(video::IMaterialRendererServices *services,
			s32 userData);
private:
	CachedPixelShaderSetting<s32> m_sm_client_map_setting{"ShadowMapClientMap"};
	CachedPixelShaderSetting<s32>
		m_sm_client_map_trans_setting{"ShadowMapClientMapTraslucent"};
	CachedPixelShaderSetting<s32>
		m_sm_dynamic_sampler_setting{"ShadowMapSamplerdynamic"};
};
