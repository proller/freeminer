/*
socket.h
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

#pragma once

#ifdef _WIN32
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include <ostream>
#include <cstring>
#include "irrlichttypes.h"
#include "networkexceptions.h"

struct IPv6AddressBytes
{
	u8 bytes[16];
	IPv6AddressBytes() { memset(bytes, 0, 16); }
};

class Address
{
public:
	Address();
	Address(u32 address, u16 port);
	Address(u8 a, u8 b, u8 c, u8 d, u16 port);
	Address(const IPv6AddressBytes *ipv6_bytes, u16 port);
	Address(const in6_addr & addr, u16 port) { setAddress(addr); setPort(port); };
	Address(const sockaddr_in6 & sai) { m_address.ipv6 = sai; m_addr_family = sai.sin6_family; m_port = ntohs(sai.sin6_port); };
	Address(const sockaddr_in & sai) { m_address.ipv4 = sai; m_addr_family = sai.sin_family; m_port = ntohs(sai.sin_port); };

	bool operator==(const Address &address) const;
	bool operator!=(const Address &address) const { return !(*this == address); }

	struct sockaddr_in getAddress() const;
	struct sockaddr_in6 getAddress6() const;
	u16 getPort() const;
	int getFamily() const { return m_addr_family; }
	bool isIPv6() const { return m_addr_family == AF_INET6; }
	bool isZero() const;
	void print(std::ostream &s) const;
	std::string serializeString() const;
	bool isLocalhost() const;

	// Resolve() may throw ResolveError (address is unchanged in this case)
	void Resolve(const char *name);

	void setAddress(u32 address);
	void setAddress(u8 a, u8 b, u8 c, u8 d);
	void setAddress(const IPv6AddressBytes *ipv6_bytes);
	void setAddress(const in6_addr & addr) { m_address.ipv6.sin6_addr = addr; m_address.ipv6.sin6_family = m_addr_family = AF_INET6; m_address.ipv6.sin6_port = ntohs(m_port); }
	void setPort(u16 port);

private:
	unsigned short m_addr_family = 0;
	union
	{
		struct sockaddr_in ipv4;
		struct sockaddr_in6 ipv6;
	} m_address = {};
	u16 m_port = 0; // Port is separate from sockaddr structures
};
