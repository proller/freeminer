/*
Minetest
Copyright (C) 2015 nerzhul, Loic Blot <loic.blot@unix-experience.fr>

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

#pragma once

#include "config.h"
#include "util/pointer.h"
#include "util/numeric.h"
#include "networkprotocol.h"
#include <SColor.h>

// fm:
#include "fm_networkprotocol.h"
#include "../util/msgpack_serialize.h"


class MsgpackPacketSafe;


inline size_t sizeof_v3opos(u16 proto_ver) {
	return proto_ver >= PROTOCOL_VERSION_32BIT ? sizeof(v3opos_t) : sizeof(v3f);
}

inline size_t sizeof_v3pos(u16 proto_ver) {
	return proto_ver >= PROTOCOL_VERSION_32BIT ? sizeof(v3pos_t) : sizeof(v3s16);
}

class NetworkPacket
{

public:
	NetworkPacket(u16 command, u32 datasize, session_t peer_id = 0, u16 proto_ver = 0);
	NetworkPacket() = default;

	~NetworkPacket();

	void setProtoVer(u16 proto_ver) { m_proto_ver = proto_ver; }
	u16 getProtoVer() { return m_proto_ver; }
	void putRawPacket(const u8 *data, u32 datasize, session_t peer_id);
	void clear();

	// Getters
	u32 getSize() const { return m_datasize; }
	session_t getPeerId() const { return m_peer_id; }
	u16 getCommand() { return m_command; }
	u32 getRemainingBytes() const { return m_datasize - m_read_offset; }
	const char *getRemainingString() { return getString(m_read_offset); }

	// Returns a c-string without copying.
	// A better name for this would be getRawString()
	const char *getString(u32 from_offset);
	// major difference to putCString(): doesn't write len into the buffer
	void putRawString(const char *src, u32 len);
	void putRawString(const std::string &src)
	{
		putRawString(src.c_str(), src.size());
	}

	NetworkPacket &operator>>(std::string &dst);
	NetworkPacket &operator<<(const std::string &src);

	void putLongString(const std::string &src);

	NetworkPacket &operator>>(std::wstring &dst);
	NetworkPacket &operator<<(const std::wstring &src);

	std::string readLongString();

	NetworkPacket &operator>>(char &dst);
	NetworkPacket &operator<<(char src);

	NetworkPacket &operator>>(bool &dst);
	NetworkPacket &operator<<(bool src);

	u8 getU8(u32 offset);

	NetworkPacket &operator>>(u8 &dst);
	NetworkPacket &operator<<(u8 src);

	u8 *getU8Ptr(u32 offset);

	u16 getU16(u32 from_offset);
	NetworkPacket &operator>>(u16 &dst);
	NetworkPacket &operator<<(u16 src);

	NetworkPacket &operator>>(u32 &dst);
	NetworkPacket &operator<<(u32 src);

	NetworkPacket &operator>>(u64 &dst);
	NetworkPacket &operator<<(u64 src);

	NetworkPacket &operator>>(float &dst);
	NetworkPacket &operator<<(float src);

	NetworkPacket &operator>>(double &dst);
	NetworkPacket &operator<<(double src);

	NetworkPacket &operator>>(v2f &dst);
	NetworkPacket &operator<<(v2f src);

	NetworkPacket &operator>>(v3f &dst);
	NetworkPacket &operator<<(v3f src);

#if USE_OPOS64
	NetworkPacket &operator>>(v3opos_t &dst);
	NetworkPacket &operator<<(v3opos_t src);
#endif

	NetworkPacket &operator>>(s16 &dst);
	NetworkPacket &operator<<(s16 src);

	NetworkPacket &operator>>(s32 &dst);
	NetworkPacket &operator<<(s32 src);

	NetworkPacket &operator>>(v2s32 &dst);
	NetworkPacket &operator<<(v2s32 src);

	NetworkPacket &operator>>(v3s16 &dst);
	NetworkPacket &operator<<(v3s16 src);

	void writeV3S32(const v3s32 &src);
	v3s32 readV3S32();

#if USE_POS32
	NetworkPacket &operator>>(v3pos_t &dst);
	NetworkPacket &operator<<(v3pos_t src);
#endif

	NetworkPacket &operator>>(video::SColor &dst);
	NetworkPacket &operator<<(video::SColor src);

	// Temp, we remove SharedBuffer when migration finished
	// ^ this comment has been here for 4 years
	Buffer<u8> oldForgePacket();

private:
	void checkReadOffset(u32 from_offset, u32 field_size);

	inline void checkDataSize(u32 field_size)
	{
		if (m_read_offset + field_size > m_datasize) {
			m_datasize = m_read_offset + field_size;
			m_data.resize(m_datasize);
		}
	}

	std::vector<u8> m_data;
	u32 m_datasize = 0;
	u32 m_read_offset = 0;
	u16 m_command = 0;
	session_t m_peer_id = 0;
	u16 m_proto_ver = 0;

// freeminer:
public:
	MsgpackPacketSafe * packet = nullptr;
	msgpack::unpacked * packet_unpacked = nullptr;
	int packet_unpack();
private:
};

bool parse_msgpack_packet(char *data, u32 datasize, MsgpackPacket *packet, int *command, msgpack::unpacked &msg);
