#pragma once

#include "session.hpp"

#include "SDL3_net/SDL_net.h"

#include "httplib.h"
#include <memory>

class Server {
public:
	Server(const std::vector<std::string> &args);

	static inline int run(const std::vector<std::string> &args) {
		return Server(args).run();
	}

	int run();
	void runLobby();
	void handleNewClient(SDLNet_StreamSocket *socket);

	void createSession(uint32_t num_players);

	// main thread -> httplib -> api point to create a new session
	// secondary thread to accept clients -> after authentification move client to session
	// 1 thread per session

private:
	bool quit;
	httplib::Server http_server;
	std::thread lobby_thread;

	std::vector<std::shared_ptr<Session>> sessions;

	SDLNet_Server *server;
};
