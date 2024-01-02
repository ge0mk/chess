#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "chess.h"

#include <SDL3_net/SDL_net.h>

#define panic(format, ...) ({ \
	fprintf(stderr, format, ##__VA_ARGS__); \
	abort(); \
})

void disconnectClient(int64_t index, void **sockets, int *num_sockets) {
	printf("client (%s) disconnected\n", SDLNet_GetAddressString(SDLNet_GetStreamSocketAddress((SDLNet_StreamSocket*)sockets[index])));
	SDLNet_DestroyStreamSocket((SDLNet_StreamSocket*)sockets[index]);
	sockets[index] = sockets[*num_sockets];
	(*num_sockets)--;
}

int main(int argc, char *argv[]) {
	if (argc != 2) {
		panic("usage: server <num_players>\n");
	}

	Field field;
	field.num_players = atoi(argv[1]);
	initializeNeighborGraph(&field);
	initializeField(&field);

	if (SDLNet_Init() != 0 ) {
		panic("Failed to initialize SDL_net %s\n", SDL_GetError());
	}

	SDLNet_Server *server = SDLNet_CreateServer(NULL, 1234);
	if (!server) {
		panic("couldn't create server: %s\n", SDL_GetError());
	}

	int num_sockets = 0, max_clients = 10;
	void **sockets = (void**)malloc(sizeof(SDLNet_Server*) + sizeof(SDLNet_StreamSocket*) * max_clients);
	sockets[num_sockets++] = server;

	while (SDLNet_WaitUntilInputAvailable(sockets, num_sockets, -1) > 0) {
		SDLNet_StreamSocket *client = NULL;
		if (SDLNet_AcceptClient(server, &client) != 0) {
			panic("couldn't accept client: %s\n", SDL_GetError());
		}

		if (client) {
			if (SDLNet_WriteToStreamSocket(client, &field, sizeof(Field)) != 0) {
				printf("client (%s) connected but couldn't send state; %s\n", SDLNet_GetAddressString(SDLNet_GetStreamSocketAddress(client)), SDL_GetError());
				SDLNet_DestroyStreamSocket(client);
			} else {
				((SDLNet_StreamSocket**)sockets)[num_sockets++] = client;
				printf("client (%s) connected (%i / %i)\n",
					SDLNet_GetAddressString(SDLNet_GetStreamSocketAddress(client)),
					num_sockets - 1, max_clients
				);
			}
		}

		for (int i = 1; i < num_sockets; i++) {
			int received = SDLNet_ReadFromStreamSocket((SDLNet_StreamSocket*)sockets[i], &field, sizeof(Field));
			if (received < 0) {
				printf("error while receiving from client (1): %s\n", SDL_GetError());
				disconnectClient(i, sockets, &num_sockets);
			} else if (received == 0) {
				continue;
			} else {
				while (received < (int)sizeof(Field)) {
					SDLNet_WaitUntilInputAvailable(&sockets[i], 1, -1);
					const int segment = SDLNet_ReadFromStreamSocket((SDLNet_StreamSocket*)sockets[i], ((char*)&field) + received, sizeof(Field) - received);
					if (segment <= 0) {
						printf("error while receiving from client (2): %s\n", SDL_GetError());
						disconnectClient(i, sockets, &num_sockets);
						break;
					}
					received += segment;
				}
			}
		}

		for (int i = 1; i < num_sockets; i++) {
			if (SDLNet_WriteToStreamSocket((SDLNet_StreamSocket*)sockets[i], &field, sizeof(Field)) != 0) {
				printf("error while sending to clients: %s\n", SDL_GetError());
				disconnectClient(i, sockets, &num_sockets);
			}
		}
	}

	SDLNet_DestroyServer(server);
	for (int i = 1; i < num_sockets; i++) {
		SDLNet_DestroyStreamSocket((SDLNet_StreamSocket*)sockets[i]);
	}

	SDLNet_Quit();

	return 0;
}
