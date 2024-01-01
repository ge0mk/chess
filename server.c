#include "chess.h"
#include "networking.h"

#include <SDL2/SDL_net.h>

void disconnectClient(int64_t index, TCPsocket *sockets, uint64_t *num_sockets, SDLNet_SocketSet set) {
	printf("client (%s) disconnected\n", SDLNet_ResolveIP(SDLNet_TCP_GetPeerAddress(sockets[index])));
	SDLNet_TCP_Close(sockets[index]);
	SDLNet_TCP_DelSocket(set, sockets[index]);
	sockets[index] = sockets[(*num_sockets) - 1];
	(*num_sockets)--;
}

int main([[maybe_unused]] int argc, char *argv[]) {
	Field field = (Field) {
		.num_players = atoi(argv[1])
	};
	createField(&field);

	if (SDLNet_Init() != 0 ) {
		panic("Failed to initialize SDL_net\n");
	}

	TCPsocket server;
	SDLNet_SocketSet server_set = SDLNet_AllocSocketSet(1);
	SDLNet_SocketSet socket_set = SDLNet_AllocSocketSet(10);
	TCPsocket sockets[10];
	uint64_t num_sockets = 0;

	server = SDLNet_TCP_Open_Address(NULL, 1234);
	SDLNet_TCP_AddSocket(server_set, server);

	while(true) {
		if (SDLNet_CheckSockets(server_set, 0) > 0) {
			const TCPsocket client = SDLNet_TCP_Accept(server);
			if (client) {
				if (SDLNet_TCP_Send(client, &field, sizeof(field)) == sizeof(field)) {
					SDLNet_TCP_AddSocket(socket_set, client);
					sockets[num_sockets++] = client;
					printf("client (%s) connected (%lu / %lu)\n", SDLNet_ResolveIP(SDLNet_TCP_GetPeerAddress(client)), num_sockets, sizeof(sockets) / sizeof(TCPsocket));
				}
			}
		}

		if (SDLNet_CheckSockets(socket_set, 0) > 0) {
			for (uint64_t i = 0; i < num_sockets; i++) {
				if (!SDLNet_SocketReady(sockets[i])) {
					continue;
				}

				if (SDLNet_TCP_Recv_Full(sockets[i], &field, sizeof(field)) < 0) {
					disconnectClient(i, sockets, &num_sockets, socket_set);
				}
			}

			for (uint64_t i = 0; i < num_sockets; i++) {
				if (SDLNet_TCP_Send(sockets[i], &field, sizeof(field)) != sizeof(field)) {
					disconnectClient(i, sockets, &num_sockets, socket_set);
				}
			}
		}
	}

	SDLNet_TCP_Close(server);
	for (uint64_t i = 0; i < num_sockets; i++) {
		SDLNet_TCP_Close(sockets[i]);
	}

	SDLNet_FreeSocketSet(server_set);
	SDLNet_FreeSocketSet(socket_set);
	SDLNet_Quit();

	return 0;
}
