#include "shared.h"

void createEdge(Field *field, uint32_t a, uint32_t b) {
	if (getZ(a) != getZ(b)) {
		field->tiles[a].neighbors[North] = packIdAndDirection(b, South);
		field->tiles[b].neighbors[North] = packIdAndDirection(a, South);
	} else if (getX(a) < getX(b)) {
		field->tiles[a].neighbors[West] = packIdAndDirection(b, West);
		field->tiles[b].neighbors[East] = packIdAndDirection(a, East);
	} else {// (getY(a) < getY(b)) {
		field->tiles[a].neighbors[North] = packIdAndDirection(b, North);
		field->tiles[b].neighbors[South] = packIdAndDirection(a, South);
	}
}

void createField(Field *field) {
	for (uint64_t id = 0; id < field->num_players * 32; id++) {
		field->tiles[id] = (Tile){
			.id = id,
			.neighbors = {UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX},
		};
	}

	for (uint64_t z = 0; z < field->num_players; z++) {
		for (uint64_t y = 0; y < 3; y++) {
			for (uint64_t x = 0; x < 7; x++) {
				createEdge(field, getId(x, y, z), getId(x + 1, y, z));
				createEdge(field, getId(x, y, z), getId(x, y + 1, z));
			}
		}

		for (uint64_t x = 0; x < 7; x++) {
			createEdge(field, getId(x, 3, z), getId(x + 1, 3, z));
		}

		for (uint64_t y = 0; y < 3; y++) {
			createEdge(field, getId(7, y, z), getId(7, y + 1, z));
		}

		for (uint64_t x = 0; x < 4; x++) {
			createEdge(field, getId(x, 3, z), getId(7 - x, 3, (z + 1) % field->num_players));
		}

		for (uint64_t x = 0; x < 8; x++) {
			field->tiles[getId(x, 1, z)].figure = Pawn;
			field->tiles[getId(x, 1, z)].player = z;
			field->tiles[getId(x, 0, z)].player = z;
		}

		field->tiles[getId(0, 0, z)].figure = Rook;
		field->tiles[getId(1, 0, z)].figure = Knight;
		field->tiles[getId(2, 0, z)].figure = Bishop;
		field->tiles[getId(3, 0, z)].figure = Queen;
		field->tiles[getId(4, 0, z)].figure = King;
		field->tiles[getId(5, 0, z)].figure = Bishop;
		field->tiles[getId(6, 0, z)].figure = Knight;
		field->tiles[getId(7, 0, z)].figure = Rook;
	}
}

void markReachableNodes(Field *field, uint64_t start, uint64_t type, bool is_on_opposing_half, bool is_on_spawn_field) {
	#define isValidId(id) (id < field->num_players * 32)

	#define forward(pos) ({ \
		const uint64_t _pos = pos; \
		isValidId(extractId(_pos)) ? field->tiles[extractId(_pos)].neighbors[extractDirection(_pos)] : _pos; \
	})

	#define right(pos) ({ \
		const uint64_t _pos = pos; \
		isValidId(extractId(_pos)) ? field->tiles[extractId(_pos)].neighbors[(extractDirection(_pos) + 1) % 4] : _pos; \
	})

	#define left(pos) ({ \
		const uint64_t _pos = pos; \
		isValidId(extractId(_pos)) ? field->tiles[extractId(_pos)].neighbors[(extractDirection(_pos) + 3) % 4] : _pos; \
	})

	#define diagonalRight(pos) left(right(pos))
	#define diagonalLeft(pos) right(left(pos))

	#define markReachable(id) ({ \
		const uint64_t _id = id; \
		if (isValidId(_id)) { \
			field->tiles[_id].is_reachable = true; \
		} \
	})

	switch (type) {
		case None: break;
		case Pawn: {
			const uint64_t current = forward(packIdAndDirection(start, is_on_opposing_half ? South : North));
			markReachable(extractId(current));
			if (is_on_spawn_field) {
				markReachable(extractId(forward(current)));
			}
		} break;
		case Bishop: {
			for (uint64_t d = North; d <= West; d++) {
				uint64_t current = diagonalRight(packIdAndDirection(start, d));
				for (uint64_t i = 0; i < 8; i++) {
					markReachable(extractId(current));
					current = diagonalRight(current);
				}

				current = diagonalLeft(packIdAndDirection(start, d));
				for (uint64_t i = 0; i < 8; i++) {
					markReachable(extractId(current));
					current = diagonalLeft(current);
				}
			}
		} break;
		case Knight: {
			for (uint64_t d = North; d <= West; d++) {
				const uint64_t current = packIdAndDirection(start, d);
				markReachable(extractId(right(forward(forward(current)))));
				markReachable(extractId(left(forward(forward(current)))));
				markReachable(extractId(forward(right(forward(current)))));
				markReachable(extractId(forward(left(forward(current)))));
			}
		} break;
		case Rook: {
			for (uint64_t d = North; d <= West; d++) {
				uint64_t current = forward(packIdAndDirection(start, d));
				for (uint64_t i = 0; i < 8; i++) {
					markReachable(extractId(current));
					current = forward(current);
				}
			}
		} break;
		case Queen: {
			markReachableNodes(field, start, Bishop, is_on_opposing_half, is_on_spawn_field);
			markReachableNodes(field, start, Rook, is_on_opposing_half, is_on_spawn_field);
		} break;
		case King: {
			for (uint64_t d = North; d <= West; d++) {
				const uint64_t current = forward(packIdAndDirection(start, d));
				markReachable(extractId(current));
				markReachable(extractId(right(current)));
				markReachable(extractId(left(current)));
			}
		} break;
		default: break;
	}

	#undef isValidId
	#undef forward
	#undef right
	#undef left
	#undef diagonalRight
	#undef diagonalLeft
	#undef markReachable
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
