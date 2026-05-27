// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2010-2013 celeron55, Perttu Ahola <celeron55@gmail.com>

#pragma once

#include "config.h"
#include <string>

class Settings;

class PIDFileHandler {
private:
	std::string m_pidfile;
	bool m_created;

public:
	PIDFileHandler(const Settings &cmd_args);
	~PIDFileHandler();
	bool isCreated() const { return m_created; }
};