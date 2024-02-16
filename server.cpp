#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <iostream>
#include <string>
#include <vector>

#include "chess.h"

#include <SDL3_net/SDL_net.h>

#define panic(format, ...) (fprintf(stderr, format, ##__VA_ARGS__), abort())

class Server {
public:
	Server(uint32_t num_players, uint16_t port) : field(num_players) {
		if (SDLNet_Init() != 0 ) {
			panic("Failed to initialize SDL_net %s\n", SDL_GetError());
		}

		server = SDLNet_CreateServer(NULL, port);
		if (!server) {
			panic("couldn't create server: %s\n", SDL_GetError());
		}
		sockets.push_back(server);

		field.initializeNeighborGraph();
		field.initializeField();
	}

	~Server() {
		SDLNet_DestroyServer(server);
		for (size_t i = 1; i < sockets.size(); i++) {
			if (sockets[i]) {
				SDLNet_DestroyStreamSocket((SDLNet_StreamSocket*)sockets[i]);
			}
		}

		SDLNet_Quit();
	}

	int64_t run() {
		while (SDLNet_WaitUntilInputAvailable(sockets.data(), sockets.size(), -1) > 0) {
			SDLNet_StreamSocket *client = NULL;
			if (SDLNet_AcceptClient(server, &client) != 0) {
				panic("couldn't accept client: %s\n", SDL_GetError());
			}

			if (client) {
				acceptClient(client);
			}

			for (size_t i = 0; i < clients.size(); i++) {
				if (!clients[i]) {
					continue;
				}

				Message msg;
				int received = SDLNet_ReadFromStreamSocket(clients[i], &msg, sizeof(Message));
				if (received == 0) {
					continue;
				}

				if (received != sizeof(Message)) {
					printf("error while receiving from client (1): %s\n", SDL_GetError());
					disconnectClient(i);
					continue;
				}

				handleMessage(i, msg);
			}
		}

		return 0;
	}

	void acceptClient(SDLNet_StreamSocket *client) {
		size_t index = 0;
		bool found = false;
		for (size_t i = 0; i < clients.size(); i++) {
			if (!clients[i]) {
				index = i;
				found = true;
				break;
			}
		}

		if (!found) {
			clients.push_back(NULL);
			index = clients.size() - 1;
		}

		field.player_pov = index;
		if (SDLNet_WriteToStreamSocket(client, &field, sizeof(Field)) != 0) {
			printf("client (%s) connected but couldn't send state; %s\n", SDLNet_GetAddressString(SDLNet_GetStreamSocketAddress(client)), SDL_GetError());
			SDLNet_DestroyStreamSocket(client);
		} else {
			clients[index] = client;
			sockets.push_back(client);

			printf("client (%s) connected %lu\n",
				SDLNet_GetAddressString(SDLNet_GetStreamSocketAddress(client)), index
			);
		}
	}

	void disconnectClient(int64_t index) {
		printf("client (%s) disconnected\n", SDLNet_GetAddressString(SDLNet_GetStreamSocketAddress(clients[index])));
		SDLNet_DestroyStreamSocket(clients[index]);
		sockets.erase(sockets.begin() + index + 1);
		clients[index] = NULL;
	}

	void handleMessage(uint32_t sender, Message msg) {
		switch (msg.type) {
			case Message::Move: {
				if (sender != field.current_player) {
					return;
				}

				field.moveFigure(msg.move.type, msg.move.from, msg.move.to);

				if (field.tiles[msg.move.to].figure != Figure::Pawn || getY(msg.move.to) != 0) {
					field.switchToNextPlayer();
					msg.next_player = field.current_player;
				}

				sendMessageToAllClients(msg);
			} break;
			case Message::Promotion: {
				if (sender != field.current_player) {
					return;
				}

				if (field.tiles[msg.promotion.id].figure == Figure::Pawn && getY(msg.promotion.id) == 0) {
					field.tiles[msg.promotion.id].figure = msg.promotion.figure;
				}

				field.switchToNextPlayer();
				msg.next_player = field.current_player;
				sendMessageToAllClients(msg);
			}
		}
	}

	void sendMessageToAllClients(Message msg) {
		for (size_t i = 0; i < clients.size(); i++) {
			if (!clients[i]) {
				continue;
			}

			if (SDLNet_WriteToStreamSocket(clients[i], &msg, sizeof(Message)) != 0) {
				printf("error while sending to clients: %s\n", SDL_GetError());
				disconnectClient(i);
			}
		}
	}

private:
	Field field;
	SDLNet_Server *server;
	std::vector<SDLNet_StreamSocket*> clients;
	std::vector<void*> sockets;
};

int main(int argc, char *argv[]) {
	uint32_t num_players = 0;
	uint16_t port = 1234;

	std::vector<std::string> args(argv, argv + argc);
	for (uint64_t i = 1; i < args.size(); i++) {
		if ((args[i] == "-n" || args[i] == "--num-players") && i < args.size() - 1) {
			num_players = std::stoul(args[i + 1]);
		} else if ((args[i] == "-p" || args[i] == "--port") && i < args.size() - 1) {
			port = std::stoul(args[i + 1]);
		} else if (args[i] == "-h" || args[i] == "--help" || args[i] == "-?") {
			std::cout<<std::format(
				"Usage: {} [OPTION]...\n\n"
				"Options:\n"
				"  -h, --help, -?                    show this help message\n"
				"  -n, --num-players <num players>   specify the number of players\n"
				"  -p, --port <port>                 specify the port to listen on\n",
				args[0]
			);
		}
	}

	if (num_players < 2) {
		return 1;
	}

	Server server(num_players, port);
	return server.run();
}
