#include "util.h"

#include <SDL2/SDL_net.h>

TCPsocket SDLNet_TCP_Open_Address(const char *address, uint16_t port);
int SDLNet_TCP_Recv_Full(TCPsocket socket, void *buffer, size_t size);
