#include "networking.h"

TCPsocket SDLNet_TCP_Open_Address(const char *address, uint16_t port) {
	IPaddress ip;
	SDLNet_ResolveHost(&ip, address, port);
	return SDLNet_TCP_Open(&ip);
}

int SDLNet_TCP_Recv_Full(TCPsocket socket, void *buffer, size_t size) {
	uint8_t *dst = (uint8_t*)buffer;
	uint64_t received = SDLNet_TCP_Recv(socket, dst, size);
	while (received < size) {
		const int64_t tmp = SDLNet_TCP_Recv(socket, dst + received, size - received);
		if (tmp <= 0) {
			return -1;
		}
		received += tmp;
	}
	return size;
}
