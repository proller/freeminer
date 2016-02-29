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

const static std::string ask_str = "{\"cmd\":\"ask\"}";
const static unsigned short int adv_port = 29998;

lan_adv::lan_adv() /*: socket(true)*/ { }

void lan_adv::ask() {
	errorstream << "ASK\n";
	reanimate();
	try {
		struct addrinfo hints { };
		//{.ai_family = AF_UNSPEC};
		//hints.ai_family = AF_UNSPEC;
		struct addrinfo *info;

		//errorstream<<__LINE__<<": "<< " " << "\n";

		if(getaddrinfo("ff02::1", nullptr, &hints, &info)) {
			//errorstream<<__LINE__<<": "<< " " << "\n";
			errorstream << "getaddrinfo fail:";
			perror("getaddrinfo");
			return;
		}
		//errorstream<<__LINE__<<": "<< " " << "\n";
		sockaddr_in6 addr = *((struct sockaddr_in6*)info->ai_addr);
		//errorstream<<__LINE__<<": "<< " " << "\n";
		addr.sin6_port = adv_port;
		//errorstream<<__LINE__<<": "<< " " << "\n";
//errorstream<<"fam="<<addr.sin6_family<< " i6="<<AF_INET6<<std::endl;
//errorstream<<ask_str << " : " << ask_str.size()<<"\n";
		UDPSocket socket_send(true);
		socket_send.Send(Address(addr), ask_str.c_str(), ask_str.size());
		freeaddrinfo(info);
//#"ff02::1"
	} catch(std::exception e) {
		errorstream << "ask fail " << e.what() << "\n";

	}
}

std::string lan_adv::get() {
	return "";
}

void lan_adv::serve(unsigned short port) {
	server = port;

	errorstream << m_name << "Serve!: " << port <<  std::endl;

	reanimate();
}

void * lan_adv::run() {

	reg("LanAdv" + (server ? std::string("Server") : std::string("Client")));
	errorstream << " run thread " << m_name << "\n";

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
	std::string answer_str;
	if (server) {
		Json::FastWriter writer;
		Json::Value answer_json;
		answer_json["port"] = server;
		answer_str = writer.write(answer_json);
	}
	while(!stopRequested()) {
		//auto time_now = porting::getTimeMs();
		try {
			Address addr;
			int rlen = socket_recv.Receive(addr, buffer, packet_maxsize);
			if (rlen <= 0)
				continue;
			std::string recd(buffer, rlen);

			//errorstream << " a=" << addr.serializeString() << " : " << addr.getPort() << " l=" << rlen << " b=" << recd << " ;  server=" << server << "\n";
			if (server) {
				if (ask_str == recd) {
					//errorstream << " answ! " << recd << "\n";
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
					collected.set(addr.serializeString() + ":" + s["port"].asString(), s);
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
	return nullptr;

}
