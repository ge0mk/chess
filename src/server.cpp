#include "server.hpp"

#include "SDL3_net/SDL_net.h"
#include "io.hpp"
#include "session.hpp"
#include <cstdint>
#include <memory>

Server::Server(const std::vector<std::string> &) : http_server() {
	server = SDLNet_CreateServer(nullptr, 1234);
	if (!server) {
		panic("couldn't create server: %s\n", SDL_GetError());
	}
}

int Server::run() {
	lobby_thread = std::thread([this](){ runLobby(); });
	http_server.listen("0.0.0.0", 8080);
	return 0;
}

void Server::runLobby() {
	while (!quit && SDLNet_WaitUntilInputAvailable((void**)&server, 1, 100) >= 0) {
		SDLNet_StreamSocket *client;
		if (SDLNet_AcceptClient(server, &client) != 0) {
			eprintln("couldn't accept client: {}\n", SDL_GetError());
			break;
		}

		if (client) {
			handleNewClient(client);
		}
	}

	if (!quit) {
		// an error occured -> stop httplib server
		http_server.stop();
	}
}

void Server::handleNewClient(SDLNet_StreamSocket *socket) {
	Message msg;
	if (Session::receiveBlocking<Message>(socket, &msg, 1) == 0) {
		assert(msg.type == Message::Join);
		std::string tmp(17, '\0');
		memcpy(tmp.data(), msg.join.name, 16);
		Player player{tmp.c_str()};
		const uint32_t session = msg.join.session;
		if (session < sessions.size()) {
			sessions[session]->addClientToQueue(player, msg.player);
		} else {
			const Message response = Message::makeReject();
			SDLNet_WriteToStreamSocket(socket, &response, sizeof(Message));
			SDLNet_DestroyStreamSocket(socket);
		}
	}
}

void Server::createSession(uint32_t num_players) {
	std::shared_ptr<Session> session = std::make_shared<Session>();
	session->initHost(num_players, {});
	session->launchThread();
	sessions.push_back(session);
}
