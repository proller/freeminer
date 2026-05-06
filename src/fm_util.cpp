// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2010-2013 celeron55, Perttu Ahola <celeron55@gmail.com>

#include "fm_util.h"
#include "filesys.h"
#include "log.h"
#include "porting.h"
#include "settings.h"
#include <fstream>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

PIDFileHandler::PIDFileHandler(const Settings &cmd_args)
	: m_created(false)
{
	if (!cmd_args.exists("pid"))
		return;

	m_pidfile = cmd_args.get("pid");
	if (m_pidfile.empty())
		return;

	std::ofstream file(m_pidfile);
	if (!file.is_open()) {
		errorstream << "Failed to create PID file: " << m_pidfile << std::endl;
		return;
	}

#ifdef _WIN32
	file << GetCurrentProcessId() << std::endl;
#else
	file << getpid() << std::endl;
#endif
	file.close();

	verbosestream << "Created PID file: " << m_pidfile << std::endl;
	m_created = true;
}

PIDFileHandler::~PIDFileHandler()
{
	if (m_created) {
		if (std::remove(m_pidfile.c_str()) == 0) {
			verbosestream << "Removed PID file: " << m_pidfile << std::endl;
		} else {
			errorstream << "Failed to remove PID file: " << m_pidfile << std::endl;
		}
	}
}