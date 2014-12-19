/*
Minetest
Copyright (C) 2010-2013 celeron55, Perttu Ahola <celeron55@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation; either version 2.1 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "settings.h"
#include "irrlichttypes_bloated.h"
#include "exceptions.h"
#include "jthread/jmutexautolock.h"
#include "strfnd.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include "debug.h"
#include "log.h"
#include "util/serialize.h"
#include "filesys.h"
#include "noise.h"
#include <cctype>
#include <algorithm>

#include "util/lock.h"


Settings::~Settings()
{
	clear();
}

Settings main_settings;
Settings *g_settings = &main_settings;
std::string g_settings_path;

Settings & Settings::operator += (const Settings &other)
{
	update(other);

	return *this;
}


Settings & Settings::operator = (const Settings &other)
{
	if (&other == this)
		return *this;

	JMutexAutoLock lock(m_mutex);
	JMutexAutoLock lock2(other.m_mutex);

	clearNoLock();
	updateNoLock(other);

	return *this;
}


bool Settings::checkNameValid(const std::string &name)
{
	size_t pos = name.find_first_of("\t\n\v\f\r\b =\"{}#");
	if (pos != std::string::npos) {
		errorstream << "Invalid character '" << name[pos]
			<< "' found in setting name" << std::endl;
		return false;
	}
	return true;
}


bool Settings::checkValueValid(const std::string &value)
{
	if (value.substr(0, 3) == "\"\"\"" ||
		value.find("\n\"\"\"") != std::string::npos) {
		errorstream << "Invalid character sequence '\"\"\"' found in"
			" setting value" << std::endl;
		return false;
	}
	return true;
}


std::string Settings::sanitizeName(const std::string &name)
{
	std::string n(name);

	for (const char *s = "\t\n\v\f\r\b =\"{}#"; *s; s++)
		n.erase(std::remove(n.begin(), n.end(), *s), n.end());

	return n;
}


std::string Settings::sanitizeValue(const std::string &value)
{
	std::string v(value);
	size_t p = 0;

	if (v.substr(0, 3) == "\"\"\"")
		v.erase(0, 3);

	while ((p = v.find("\n\"\"\"")) != std::string::npos)
		v.erase(p, 4);

	return v;
}


std::string Settings::getMultiline(std::istream &is, size_t *num_lines)
{
	size_t lines = 1;
	std::string value;
	std::string line;

	while (is.good()) {
		lines++;
		std::getline(is, line);
		if (line == "\"\"\"")
			break;
		value += line;
		value.push_back('\n');
	}

	size_t len = value.size();
	if (len)
		value.erase(len - 1);

	if (num_lines)
		*num_lines = lines;

	return value;
}


bool Settings::readConfigFile(const char *filename)
{
	std::ifstream is(filename);
	if (!is.good())
		return false;

	return parseConfigLines(is, "");
}


bool Settings::parseConfigLines(std::istream &is, const std::string &end)
{
	JMutexAutoLock lock(m_mutex);

	std::string line, name, value;

	while (is.good()) {
		std::getline(is, line);
		SettingsParseEvent event = parseConfigObject(line, end, name, value);

		switch (event) {
		case SPE_NONE:
		case SPE_INVALID:
		case SPE_COMMENT:
			break;
		case SPE_KVPAIR:
			m_settings[name] = SettingsEntry(value);
			break;
		case SPE_END:
			return true;
		case SPE_GROUP: {
			Settings *group = new Settings;
			if (!group->parseConfigLines(is, "}")) {
				delete group;
				return false;
			}
			m_settings[name] = SettingsEntry(group);
			break;
		}
		case SPE_MULTILINE:
			m_settings[name] = SettingsEntry(getMultiline(is));
			break;
		}
	}

	return end.empty();
}


void Settings::writeLines(std::ostream &os, u32 tab_depth) const
{
	JMutexAutoLock lock(m_mutex);

	for (std::map<std::string, SettingsEntry>::const_iterator
			it = m_settings.begin();
			it != m_settings.end(); ++it)
		printEntry(os, it->first, it->second, tab_depth);
}


void Settings::printEntry(std::ostream &os, const std::string &name,
	const SettingsEntry &entry, u32 tab_depth)
{
	for (u32 i = 0; i != tab_depth; i++)
		os << "\t";

	if (entry.is_group) {
		os << name << " = {\n";

		entry.group->writeLines(os, tab_depth + 1);

		for (u32 i = 0; i != tab_depth; i++)
			os << "\t";
		os << "}\n";
	} else {
		os << name << " = ";

		if (entry.value.find('\n') != std::string::npos)
			os << "\"\"\"\n" << entry.value << "\n\"\"\"\n";
		else
			os << entry.value << "\n";
	}
}


bool Settings::updateConfigObject(std::istream &is, std::ostream &os,
	const std::string &end, u32 tab_depth)
{
	std::map<std::string, SettingsEntry>::const_iterator it;
	std::set<std::string> present_entries;
	std::string line, name, value;
	bool was_modified = false;
	bool end_found = false;

	// Add any settings that exist in the config file with the current value
	// in the object if existing
	while (is.good() && !end_found) {
		std::getline(is, line);
		SettingsParseEvent event = parseConfigObject(line, end, name, value);

		switch (event) {
		case SPE_END:
			os << line << (is.eof() ? "" : "\n");
			end_found = true;
			break;
		case SPE_MULTILINE:
			value = getMultiline(is);
			/* FALLTHROUGH */
		case SPE_KVPAIR:
			it = m_settings.find(name);
			if (it != m_settings.end() &&
				(it->second.is_group || it->second.value != value)) {
				printEntry(os, name, it->second, tab_depth);
				was_modified = true;
			} else {
				os << line << "\n";
				if (event == SPE_MULTILINE)
					os << value << "\n\"\"\"\n";
			}
			present_entries.insert(name);
			break;
		case SPE_GROUP:
			it = m_settings.find(name);
			if (it != m_settings.end() && it->second.is_group) {
				os << line << "\n";
				assert(it->second.group != NULL);
				was_modified |= it->second.group->updateConfigObject(is, os,
					"}", tab_depth + 1);
			} else {
				printEntry(os, name, it->second, tab_depth);
				was_modified = true;
			}
			present_entries.insert(name);
			break;
		default:
			os << line << (is.eof() ? "" : "\n");
			break;
		}
	}

	// Add any settings in the object that don't exist in the config file yet
	for (it = m_settings.begin(); it != m_settings.end(); ++it) {
		if (present_entries.find(it->first) != present_entries.end())
			continue;

		printEntry(os, it->first, it->second, tab_depth);
		was_modified = true;
	}

	return was_modified;
}


bool Settings::updateConfigFile(const std::string &filename)
{
	if (filename.find(".json") != std::string::npos) {
		write_json_file(filename);
		return true;
	}

	JMutexAutoLock lock(m_mutex);

	std::ifstream is(filename);
	std::ostringstream os(std::ios_base::binary);

	bool was_modified = updateConfigObject(is, os, "");
	is.close();

	if (!was_modified)
		return true;

	if (!fs::safeWriteToFile(filename.c_str(), os.str())) {
		errorstream << "Error writing configuration file: \""
			<< filename << "\"" << std::endl;
		return false;
	}

	return true;
}


bool Settings::parseCommandLine(int argc, char *argv[],
		std::map<std::string, ValueSpec> &allowed_options)
{
	int nonopt_index = 0;
	for (int i = 1; i < argc; i++) {
		std::string arg_name = argv[i];
		if (arg_name.substr(0, 2) != "--") {
			// If option doesn't start with -, read it in as nonoptX
			if (arg_name[0] != '-'){
				std::string name = "nonopt";
				name += itos(nonopt_index);
				set(name, arg_name);
				nonopt_index++;
				continue;
			}
			errorstream << "Invalid command-line parameter \""
					<< arg_name << "\": --<option> expected." << std::endl;
			return false;
		}

		std::string name = arg_name.substr(2);

		std::map<std::string, ValueSpec>::iterator n;
		n = allowed_options.find(name);
		if (n == allowed_options.end()) {
			errorstream << "Unknown command-line parameter \""
					<< arg_name << "\"" << std::endl;
			return false;
		}

		ValueType type = n->second.type;

		std::string value = "";

		if (type == VALUETYPE_FLAG) {
			value = "true";
		} else {
			if ((i + 1) >= argc) {
				errorstream << "Invalid command-line parameter \""
						<< name << "\": missing value" << std::endl;
				return false;
			}
			value = argv[++i];
		}

		set(name, value);
	}

	return true;
}



/***********
 * Getters *
 ***********/


const SettingsEntry &Settings::getEntry(const std::string &name) const
{
	JMutexAutoLock lock(m_mutex);

	std::map<std::string, SettingsEntry>::const_iterator n;
	if ((n = m_settings.find(name)) == m_settings.end()) {
		if ((n = m_defaults.find(name)) == m_defaults.end())
			throw SettingNotFoundException("Setting [" + name + "] not found.");
	}
	return n->second;
}


Settings *Settings::getGroup(const std::string &name) const
{
	const SettingsEntry &entry = getEntry(name);
	if (!entry.is_group)
		throw SettingNotFoundException("Setting [" + name + "] is not a group.");
	return entry.group;
}


std::string Settings::get(const std::string &name) const
{
	const SettingsEntry &entry = getEntry(name);
	if (entry.is_group)
		throw SettingNotFoundException("Setting [" + name + "] is a group.");
	return entry.value;
}


bool Settings::getBool(const std::string &name) const
{
	return is_yes(get(name));
}


u16 Settings::getU16(const std::string &name) const
{
	return stoi(get(name), 0, 65535);
}


s16 Settings::getS16(const std::string &name) const
{
	return stoi(get(name), -32768, 32767);
}


s32 Settings::getS32(const std::string &name) const
{
	return stoi(get(name));
}


float Settings::getFloat(const std::string &name) const
{
	return stof(get(name));
}


u64 Settings::getU64(const std::string &name) const
{
	u64 value = 0;
	std::string s = get(name);
	std::istringstream ss(s);
	ss >> value;
	return value;
}


v2f Settings::getV2F(const std::string &name) const
{
	v2f value;
	Strfnd f(get(name));
	f.next("(");
	value.X = stof(f.next(","));
	value.Y = stof(f.next(")"));
	return value;
}


v3f Settings::getV3F(const std::string &name) const
{
	v3f value;
	Strfnd f(get(name));
	f.next("(");
	value.X = stof(f.next(","));
	value.Y = stof(f.next(","));
	value.Z = stof(f.next(")"));
	return value;
}


u32 Settings::getFlagStr(const std::string &name, const FlagDesc *flagdesc,
	u32 *flagmask) const
{
	std::string val = get(name);
	return std::isdigit(val[0])
		? stoi(val)
		: readFlagString(val, flagdesc, flagmask);
}


// N.B. if getStruct() is used to read a non-POD aggregate type,
// the behavior is undefined.
bool Settings::getStruct(const std::string &name, const std::string &format,
	void *out, size_t olen) const
{
	std::string valstr;

	try {
		valstr = get(name);
	} catch (SettingNotFoundException &e) {
		return false;
	}

	if (!deSerializeStringToStruct(valstr, format, out, olen))
		return false;

	return true;
}


bool Settings::getNoiseParams(const std::string &name, NoiseParams &np) const
{
	return getNoiseParamsFromGroup(name, np) || getNoiseParamsFromValue(name, np);
}


bool Settings::getNoiseParamsFromValue(const std::string &name,
	NoiseParams &np) const
{
	std::string value;

	if (!getNoEx(name, value))
		return false;

	Strfnd f(value);

	np.offset   = stof(f.next(","));
	np.scale    = stof(f.next(","));
	f.next("(");
	np.spread.X = stof(f.next(","));
	np.spread.Y = stof(f.next(","));
	np.spread.Z = stof(f.next(")"));
	f.next(",");
	np.seed     = stoi(f.next(","));
	np.octaves  = stoi(f.next(","));
	np.persist  = stof(f.next(","));

	std::string optional_params = f.next("");
	if (optional_params != "")
		np.lacunarity = stof(optional_params);

	return true;
}


bool Settings::getNoiseParamsFromGroup(const std::string &name,
	NoiseParams &np) const
{
	Settings *group = NULL;

	if (!getGroupNoEx(name, group))
		return false;

	group->getFloatNoEx("offset",      np.offset);
	group->getFloatNoEx("scale",       np.scale);
	group->getV3FNoEx("spread",        np.spread);
	group->getS32NoEx("seed",          np.seed);
	group->getU16NoEx("octaves",       np.octaves);
	group->getFloatNoEx("persistence", np.persist);
	group->getFloatNoEx("lacunarity",  np.lacunarity);

	np.flags = 0;
	if (!group->getFlagStrNoEx("flags", np.flags, flagdesc_noiseparams))
		np.flags = NOISE_FLAG_DEFAULTS;

	group->getFloatNoEx("farscale",    np.farscale);
	group->getFloatNoEx("farspread",   np.farspread);
	group->getFloatNoEx("farpersist",  np.farpersist);

	return true;
}


bool Settings::exists(const std::string &name) const
{
	JMutexAutoLock lock(m_mutex);

	return (m_settings.find(name) != m_settings.end() ||
		m_defaults.find(name) != m_defaults.end());
}


std::vector<std::string> Settings::getNames() const
{
	std::vector<std::string> names;
	for (std::map<std::string, SettingsEntry>::const_iterator
			i = m_settings.begin();
			i != m_settings.end(); ++i) {
		names.push_back(i->first);
	}
	return names;
}



/***************************************
 * Getters that don't throw exceptions *
 ***************************************/

bool Settings::getEntryNoEx(const std::string &name, SettingsEntry &val) const
{
	try {
		val = getEntry(name);
		return true;
	} catch (SettingNotFoundException &e) {
		return false;
	}
}


bool Settings::getGroupNoEx(const std::string &name, Settings *&val) const
{
	try {
		val = getGroup(name);
		return true;
	} catch (SettingNotFoundException &e) {
		return false;
	}
}


bool Settings::getNoEx(const std::string &name, std::string &val) const
{
	try {
		val = get(name);
		return true;
	} catch (SettingNotFoundException &e) {
		return false;
	}
}


bool Settings::getFlag(const std::string &name) const
{
	try {
		return getBool(name);
	} catch(SettingNotFoundException &e) {
		return false;
	}
}


bool Settings::getFloatNoEx(const std::string &name, float &val) const
{
	try {
		val = getFloat(name);
		return true;
	} catch (SettingNotFoundException &e) {
		return false;
	}
}


bool Settings::getU16NoEx(const std::string &name, u16 &val) const
{
	try {
		val = getU16(name);
		return true;
	} catch (SettingNotFoundException &e) {
		return false;
	}
}


bool Settings::getS16NoEx(const std::string &name, s16 &val) const
{
	try {
		val = getS16(name);
		return true;
	} catch (SettingNotFoundException &e) {
		return false;
	}
}


bool Settings::getS32NoEx(const std::string &name, s32 &val) const
{
	try {
		val = getS32(name);
		return true;
	} catch (SettingNotFoundException &e) {
		return false;
	}
}


bool Settings::getU64NoEx(const std::string &name, u64 &val) const
{
	try {
		val = getU64(name);
		return true;
	} catch (SettingNotFoundException &e) {
		return false;
	}
}


bool Settings::getV2FNoEx(const std::string &name, v2f &val) const
{
	try {
		val = getV2F(name);
		return true;
	} catch (SettingNotFoundException &e) {
		return false;
	}
}


bool Settings::getV3FNoEx(const std::string &name, v3f &val) const
{
	try {
		val = getV3F(name);
		return true;
	} catch (SettingNotFoundException &e) {
		return false;
	}
}


// N.B. getFlagStrNoEx() does not set val, but merely modifies it.  Thus,
// val must be initialized before using getFlagStrNoEx().  The intention of
// this is to simplify modifying a flags field from a default value.
bool Settings::getFlagStrNoEx(const std::string &name, u32 &val,
	FlagDesc *flagdesc) const
{
	try {
		u32 flags, flagmask;

		flags = getFlagStr(name, flagdesc, &flagmask);

		val &= ~flagmask;
		val |=  flags;

		return true;
	} catch (SettingNotFoundException &e) {
		return false;
	}
}


/***********
 * Setters *
 ***********/

bool Settings::setEntry(const std::string &name, const void *data,
	bool set_group, bool set_default)
{
	Settings *old_group = NULL;

	if (!checkNameValid(name))
		return false;
	if (!set_group && !checkValueValid(*(const std::string *)data))
		return false;

	{
		JMutexAutoLock lock(m_mutex);

		SettingsEntry &entry = set_default ? m_defaults[name] : m_settings[name];
		old_group = entry.group;

		entry.value    = set_group ? "" : *(const std::string *)data;
		entry.group    = set_group ? *(Settings **)data : NULL;
		entry.is_group = set_group;
	}

	delete old_group;

	return true;
}


bool Settings::set(const std::string &name, const std::string &value)
{
	if (!setEntry(name, &value, false, false))
		return false;

	doCallbacks(name);
	return true;
}


bool Settings::setDefault(const std::string &name, const std::string &value)
{
	return setEntry(name, &value, false, true);
}


bool Settings::setGroup(const std::string &name, Settings *group)
{
	return setEntry(name, &group, true, false);
}


bool Settings::setGroupDefault(const std::string &name, Settings *group)
{
	return setEntry(name, &group, true, true);
}


bool Settings::setBool(const std::string &name, bool value)
{
	return set(name, value ? "true" : "false");
}


bool Settings::setS16(const std::string &name, s16 value)
{
	return set(name, itos(value));
}


bool Settings::setU16(const std::string &name, u16 value)
{
	return set(name, itos(value));
}


bool Settings::setS32(const std::string &name, s32 value)
{
	return set(name, itos(value));
}


bool Settings::setU64(const std::string &name, uint64_t value)
{
	std::ostringstream os;
	os << value;
	return set(name, os.str());
}


bool Settings::setFloat(const std::string &name, float value)
{
	return set(name, ftos(value));
}


bool Settings::setV2F(const std::string &name, v2f value)
{
	std::ostringstream os;
	os << "(" << value.X << "," << value.Y << ")";
	return set(name, os.str());
}


bool Settings::setV3F(const std::string &name, v3f value)
{
	std::ostringstream os;
	os << "(" << value.X << "," << value.Y << "," << value.Z << ")";
	return set(name, os.str());
}


bool Settings::setFlagStr(const std::string &name, u32 flags,
	const FlagDesc *flagdesc, u32 flagmask)
{
	return set(name, writeFlagString(flags, flagdesc, flagmask));
}


bool Settings::setStruct(const std::string &name, const std::string &format,
	void *value)
{
	std::string structstr;
	if (!serializeStructToString(&structstr, format, value))
		return false;

	return set(name, structstr);
}


bool Settings::setNoiseParams(const std::string &name,
	const NoiseParams &np, bool set_default)
{
	Settings *group = new Settings;

	group->setFloat("offset",      np.offset);
	group->setFloat("scale",       np.scale);
	group->setV3F("spread",        np.spread);
	group->setS32("seed",          np.seed);
	group->setU16("octaves",       np.octaves);
	group->setFloat("persistence", np.persist);
	group->setFloat("lacunarity",  np.lacunarity);
	group->setFlagStr("flags",     np.flags, flagdesc_noiseparams, np.flags);

	group->setFloat("farscale",    np.farscale);
	group->setFloat("farspread",   np.farspread);
	group->setFloat("farpersist",  np.farpersist);

	return setEntry(name, &group, true, set_default);
}


bool Settings::remove(const std::string &name)
{
	JMutexAutoLock lock(m_mutex);

	delete m_settings[name].group;
	m_json.removeMember(name);
	return m_settings.erase(name);
}


void Settings::clear()
{
	JMutexAutoLock lock(m_mutex);
	clearNoLock();
}


void Settings::updateValue(const Settings &other, const std::string &name)
{
	if (&other == this)
		return;

	JMutexAutoLock lock(m_mutex);

	try {
		std::string val = other.get(name);

		m_settings[name] = val;
	} catch (SettingNotFoundException &e) {
	}
}


void Settings::update(const Settings &other)
{
	if (&other == this)
		return;

	JMutexAutoLock lock(m_mutex);
	JMutexAutoLock lock2(other.m_mutex);

	updateNoLock(other);
}


SettingsParseEvent Settings::parseConfigObject(const std::string &line,
	const std::string &end, std::string &name, std::string &value)
{
	std::string trimmed_line = trim(line);

	if (trimmed_line.empty())
		return SPE_NONE;
	if (trimmed_line[0] == '#')
		return SPE_COMMENT;
	if (trimmed_line == end)
		return SPE_END;

	size_t pos = trimmed_line.find('=');
	if (pos == std::string::npos)
		return SPE_INVALID;

	name  = trim(trimmed_line.substr(0, pos));
	value = trim(trimmed_line.substr(pos + 1));

	if (value == "{")
		return SPE_GROUP;
	if (value == "\"\"\"")
		return SPE_MULTILINE;

	return SPE_KVPAIR;
}


void Settings::updateNoLock(const Settings &other)
{
	m_settings.insert(other.m_settings.begin(), other.m_settings.end());
	m_defaults.insert(other.m_defaults.begin(), other.m_defaults.end());
}


void Settings::clearNoLock()
{
	std::map<std::string, SettingsEntry>::const_iterator it;
	for (it = m_settings.begin(); it != m_settings.end(); ++it)
		delete it->second.group;
	m_settings.clear();

	for (it = m_defaults.begin(); it != m_defaults.end(); ++it)
		delete it->second.group;
	m_defaults.clear();
	m_json.clear();
}

void Settings::registerChangedCallback(std::string name,
	setting_changed_callback cbf)
{
	m_callbacks[name].push_back(cbf);
}

void Settings::doCallbacks(const std::string name)
{
	std::vector<setting_changed_callback> tempvector;
	{
		JMutexAutoLock lock(m_mutex);
		if (m_callbacks.find(name) != m_callbacks.end())
		{
			tempvector = m_callbacks[name];
		}
	}

	std::vector<setting_changed_callback>::iterator iter;
	for (iter = tempvector.begin(); iter != tempvector.end(); iter++)
	{
		(*iter)(name);
	}
}


Json::Value Settings::getJson(const std::string & name, const Json::Value & def) {

	{
		try_shared_lock lock(m_mutex);
		if (m_json[name].isObject())
			return m_json.get(name, def);
	}

	//todo: remove later:
	Json::Value root;
	std::string value = get(name);
	if (value.empty())
		return def;
	if (!json_reader.parse( value, root ) ) {
		errorstream  << "Failed to parse json conf var [" << name << "]='" << value <<"' : " << json_reader.getFormattedErrorMessages();
	}
	return root;
}

void Settings::setJson(const std::string & name, const Json::Value & value) {
	set(name, value.empty() ? "{}" : json_writer.write( value )); //todo: remove later

	unique_lock lock(m_mutex);
	m_json[name] = value;
}

bool Settings::to_json(Json::Value &json) const {
	try_shared_lock lock(m_mutex);

	for (const auto & ir: m_settings) {
		if (ir.second.is_group && ir.second.group) {
			Json::Value v;
			ir.second.group->to_json(v);
			json[ir.first] = v;
		} else {
			json[ir.first] = ir.second.value;
		}
	}

	for (const auto & key: m_json.getMemberNames())
		json[key] = m_json[key];

	return true;
}

bool Settings::from_json(const Json::Value &json) {
	if (!json.isObject())
		return false;
	for (const auto & key: json.getMemberNames()) {
		if (json[key].isObject()) {
			//setJson(key, json[key]); // todo
			auto s = new Settings;
			s->from_json(json[key]);
			setGroup(key, s);
		}
		else if (json[key].isArray())
			setJson(key, json[key]);
		else
			set(key, json[key].asString());
	}
	return true;
}

bool Settings::write_json_file(const std::string &filename) {
	Json::Value json;
	to_json(json);

	std::ostringstream os(std::ios_base::binary);
	os << json;

	if (!fs::safeWriteToFile(filename.c_str(), os.str())) {
		errorstream << "Error writing json configuration file: \"" << filename << "\"" << std::endl;
		return false;
	}
	return true;
}

bool Settings::read_json_file(const std::string &filename) {
	std::ifstream is(filename.c_str(), std::ios_base::binary);
	if (!is.good())
		return false;
	Json::Value json;
	is >> json;
	return from_json(json);
}

void Settings::msgpack_pack(msgpack::packer<msgpack::sbuffer> &pk) const
{
	Json::Value json;
	to_json(json);
	std::ostringstream os(std::ios_base::binary);
	os << json;
	pk.pack(os.str());
}

void Settings::msgpack_unpack(msgpack::object o)
{
	std::string data;
	o.convert(&data);
	std::istringstream os(data, std::ios_base::binary);
	os >> m_json;
	from_json(m_json);
}
