#include "shared.h"

int main([[maybe_unused]] int argc, char *argv[]) {
	Field field = (Field) {
		.num_players = atoi(argv[1])
	};

	if (SDLNet_Init() != 0 ) {
		panic("Failed to initialize SDL_net\n");
	}
	atexit(SDLNet_Quit);

	TCPsocket server;
	SDLNet_SocketSet client_set = SDLNet_AllocSocketSet(10);
	TCPsocket clients[10];
	uint64_t num_clients = 0;

	SDLNet_SocketSet server_set = SDLNet_AllocSocketSet(1);
	IPaddress ip;

	SDLNet_ResolveHost(&ip, NULL, 1234);
	server = SDLNet_TCP_Open(&ip);
	SDLNet_TCP_AddSocket(server_set, server);


	while(true) {
		if (SDLNet_CheckSockets(server_set, 0) > 0) {
			const TCPsocket client = SDLNet_TCP_Accept(server);
			if (client) {
				if (SDLNet_TCP_Send(client, &field, sizeof(field)) == sizeof(field)) {
					SDLNet_TCP_AddSocket(client_set, client);
					clients[num_clients++] = client;
				}
			}
		}

		if (SDLNet_CheckSockets(client_set, 0) > 0) {
			for (uint64_t i = 0; i < num_clients; i++) {
				if (SDLNet_SocketReady(clients[i])) {
					if (SDLNet_TCP_Recv_Full(clients[i], &field, sizeof(field)) < 0) {
						panic("client stopped mid transmission\n");
					}
				}
			}

			for (uint64_t i = 0; i < num_clients; i++) {
				if (SDLNet_TCP_Send(clients[i], &field, sizeof(field)) != sizeof(field)) {
					SDLNet_TCP_Close(clients[i]);
					SDLNet_TCP_DelSocket(client_set, clients[i]);
					clients[i] = clients[num_clients - 1];
					num_clients--;
				}
			}
		}
	}

	return 0;
}
