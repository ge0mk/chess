#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "chess.h"

#include <SDL3_net/SDL_net.h>

#define panic(format, ...) ({ \
	fprintf(stderr, format, ##__VA_ARGS__); \
	abort(); \
})

class Server {
public:
	Server(uint32_t num_players) : field{.num_players = num_players} {
		if (SDLNet_Init() != 0 ) {
			panic("Failed to initialize SDL_net %s\n", SDL_GetError());
		}

		server = SDLNet_CreateServer(NULL, 1234);
		if (!server) {
			panic("couldn't create server: %s\n", SDL_GetError());
		}
		sockets.push_back(server);

		initializeNeighborGraph(&field);
		initializeField(&field);
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

	void run() {
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

				if (received < 0) {
					printf("error while receiving from client (1): %s\n", SDL_GetError());
					disconnectClient(i);
					continue;
				}

				while (received < (int)sizeof(Message)) {
					SDLNet_WaitUntilInputAvailable((void**)&clients[i], 1, -1);
					const int segment = SDLNet_ReadFromStreamSocket(clients[i], ((char*)&msg) + received, sizeof(Message) - received);
					if (segment <= 0) {
						printf("error while receiving from client (2): %s\n", SDL_GetError());
						disconnectClient(i);
						break;
					}
					received += segment;
				}

				handleMessage(i, msg);
			}
		}
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
			case Message::None: {} break;
			case Message::Move: {
				if (sender != field.current_player) {
					return;
				}

				field.tiles[msg.move.to].figure = field.tiles[msg.move.from].figure;
				field.tiles[msg.move.to].player = field.tiles[msg.move.from].player;
				field.tiles[msg.move.to].move_count = field.tiles[msg.move.from].move_count + 1;
				field.tiles[msg.move.from].figure = None;

				if (field.tiles[msg.move.to].figure == Pawn && getY(msg.move.to) == 0) {
					field.tiles[msg.move.to].figure = msg.move.promotion;
				}

				field.current_player = (field.current_player + 1) % field.num_players;
			} break;
		}

		for (size_t i = 0; i < clients.size(); i++) {
			if (!clients[i]) {
				continue;
			}

			field.player_pov = i;
			if (SDLNet_WriteToStreamSocket(clients[i], &field, sizeof(Field)) != 0) {
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
	if (argc != 2) {
		panic("usage: server <num_players>\n");
	}

	Server server{uint32_t(atoi(argv[1]))};
	server.run();

	return 0;
}
