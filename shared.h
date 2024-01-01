#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>
#include <string.h>

#include <SDL2/SDL_net.h>

#define PI 3.141592654f

#define getId(x, y, z) (((z)<<5) | ((y)<<3) | (x))
#define getX(id) ((id) & 0b00000111)
#define getY(id) (((id)>>3) & 0b00000011)
#define getXY(id) ((id) & 0b00011111)
#define getZ(id) ((id)>>5)
#define packIdAndDirection(id, direction) (((id)<<2) | (direction))
#define extractId(v) ((v)>>2)
#define extractDirection(v) ((v) & 0b00000011)

#define min(a, b) ({ \
	const typeof(a) _a = a; \
	const typeof(b) _b = b; \
	_a < _b ? _a : _b; \
})

#define max(a, b) ({ \
	const typeof(a) _a = a; \
	const typeof(b) _b = b; \
	_a > _b ? _a : _b; \
})

#define clamp(a, min_val, max_val) (max(min(a, max_val), min_val))

#define panic(format, ...) ({ \
	fprintf(stderr, format, ##__VA_ARGS__); \
	exit(EXIT_FAILURE); \
})

#define MAX_PLAYERS 10

enum Direction {
	North,
	East,
	South,
	West,
};

typedef enum FigureType {
	None,
	Pawn,
	Bishop,
	Knight,
	Rook,
	Queen,
	King,
} FigureType;

typedef struct {
	uint32_t neighbors[4];
	uint32_t id;
	uint32_t figure;
	uint32_t player;
	uint32_t is_reachable;
} Tile;

typedef struct {
	Tile tiles[32 * MAX_PLAYERS];
	uint32_t num_players;
	uint32_t cursor_id;
	uint32_t selected_id;
} Field;

void createEdge(Field *field, uint32_t a, uint32_t b);
void createField(Field *field);
void markReachableNodes(Field *field, uint64_t start, uint64_t type, bool is_on_opposing_half, bool is_on_spawn_field);
int SDLNet_TCP_Recv_Full(TCPsocket socket, void *buffer, size_t size);
