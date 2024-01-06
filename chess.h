#include <cstdint>

#define getId(x, y, z) (((z)<<5) | ((y)<<3) | (x))
#define getX(id) ((id) & 0b00000111)
#define getY(id) (((id)>>3) & 0b00000011)
#define getXY(id) ((id) & 0b00011111)
#define getZ(id) ((id)>>5)
#define packIdAndDirection(id, direction) (((id)<<2) | (direction))
#define extractId(v) ((v)>>2)
#define extractDirection(v) ((v) & 0b00000011)

#define MAX_PLAYERS 10

enum Direction {
	North,
	East,
	South,
	West,
};

enum FigureType {
	None,
	Pawn,
	Bishop,
	Knight,
	Rook,
	Queen,
	King,
};

struct Tile {
	uint8_t figure;
	uint8_t player;
	uint8_t is_reachable;
	uint8_t was_moved;
};

struct Field {
	uint32_t neighbors[32 * MAX_PLAYERS][4];
	Tile tiles[32 * MAX_PLAYERS];
	uint32_t cursor_id;
	uint32_t num_players;
	uint32_t selected_id;
	uint32_t player;
};

void createEdge(Field *field, uint32_t a, uint32_t b);
void initializeNeighborGraph(Field *field);
void initializeField(Field *field);
void markReachableNodes(Field *field, uint64_t start, uint64_t type);
