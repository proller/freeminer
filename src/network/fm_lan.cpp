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

#include "fm_lan.h"
#include "../socket.h"
#include "../util/string.h"
#include "../log_types.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

const static unsigned short int adv_port = 29998;
const static std::string ask_str = "{\"cmd\":\"ask\"}";

lan_adv::lan_adv() { }

void lan_adv::ask() {
	reanimate();
	send_string(ask_str);
}

void lan_adv::send_string(std::string str) {
		struct addrinfo hints { };
		struct addrinfo *result;

		if(getaddrinfo("ff02::1", nullptr, &hints, &result)) {
			return;
		}
		for (auto info = result; info; info = info->ai_next) {
	try {

		sockaddr_in6 addr = *((struct sockaddr_in6*)info->ai_addr);
		addr.sin6_port = adv_port;
		UDPSocket socket_send(true);
		socket_send.Send(Address(addr), str.c_str(), str.size());
	} catch(std::exception e) {
		errorstream << " send fail " << e.what() << "\n";
	}
		}
		freeaddrinfo(result);
}

std::string lan_adv::get() {
	return "";
}

void lan_adv::serve(unsigned short port) {
	server = port;
	//errorstream << m_name << "Serve!: " << port <<  std::endl;
	reanimate();
}

void * lan_adv::run() {

	reg("LanAdv" + (server ? std::string("Server") : std::string("Client")));

	UDPSocket socket_recv(true);
	int set_option_on = 1;
	setsockopt(socket_recv.GetHandle(), SOL_SOCKET, SO_REUSEADDR, (const char*) &set_option_on, sizeof(set_option_on));
#ifdef SO_REUSEPORT
	setsockopt(socket_recv.GetHandle(), SOL_SOCKET, SO_REUSEPORT, (const char*) &set_option_on, sizeof(set_option_on));
#endif
	socket_recv.setTimeoutMs(1000);
	Address addr_bind(in6addr_any, adv_port);
	socket_recv.Bind(addr_bind);

	unsigned int packet_maxsize = 16384;
	char buffer [packet_maxsize];
	Json::Reader reader;
	Json::FastWriter writer;
	std::string answer_str;
	if (server) {
		Json::Value answer_json;
		answer_json["port"] = server;
		answer_str = writer.write(answer_json);
		send_string(answer_str);
	}
	while(!stopRequested()) {
		try {
			Address addr;
			int rlen = socket_recv.Receive(addr, buffer, packet_maxsize);
			if (rlen <= 0)
				continue;
			std::string recd(buffer, rlen);

			if (ask_str == recd) {
				errorstream << " " << addr.serializeString() << " want play " << "\n";
			}
			//errorstream << " a=" << addr.serializeString() << " : " << addr.getPort() << " l=" << rlen << " b=" << recd << " ;  server=" << server << "\n";
			if (server) {
				if (ask_str == recd) {
					UDPSocket socket_send(true);
					addr.setPort(adv_port);
					socket_send.Send(addr, answer_str.c_str(), answer_str.size());
				}
			} else {
				Json::Value s;
				if (!reader.parse(recd, s))
					continue;
				if (s["port"].isInt()) {
					s["address"] = addr.serializeString();
					auto key = addr.serializeString() + ":" + s["port"].asString();
					if (s["cmd"].asString() == "shutdown") {
						errorstream << "server shutdown "<< key << "\n";
						collected.erase(key);
					} else {
						if (!collected.count(key))
							errorstream << "server start "<< key << "\n";
						collected.set(key, s);
					}
				}
			}

#if !EXEPTION_DEBUG
		} catch(std::exception &e) {
			errorstream << m_name << ": exception: " << e.what() << std::endl;
		} catch (...) {
			errorstream << m_name << ": Ooops..." << std::endl;
#else
		} catch (int) { //nothing
#endif
		}
	}

	if (server) {
		Json::Value answer_json;
		answer_json["port"] = server;
		answer_json["cmd"] = "shutdown";
		send_string(writer.write(answer_json));
	}

	return nullptr;

}
