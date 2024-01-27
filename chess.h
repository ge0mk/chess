#include <cstdint>

#define getId(x, y, z) (((z)<<5) | ((y)<<3) | (x))
#define getX(id) ((id) & 0b00000111)
#define getY(id) (((id)>>3) & 0b00000011)
#define getXY(id) ((id) & 0b00011111)
#define getZ(id) ((id)>>5)
#define packIdAndDirection(id, direction) (((id)<<2) | (direction))
#define extractId(v) ((v)>>2)
#define extractDirection(v) ((v) & 0b00000011)

#define MAX_PLAYERS 8

enum Direction {
	North,
	East,
	South,
	West,
};

enum class Figure : uint8_t {
	None,
	Pawn,
	Bishop,
	Knight,
	Rook,
	Queen,
	King,
	Any,
};

enum class MoveType : uint8_t {
	None,
	Move,
	Capture,
	Castle,
	EnPassant,
};

struct Tile {
	Figure figure;
	uint8_t player;
	MoveType move;
	uint8_t move_count;
};

struct PlayerData {
	bool is_checkmate;
	uint32_t king_position;
};

struct Field {
	uint32_t neighbors[32 * MAX_PLAYERS][4];
	PlayerData players[MAX_PLAYERS];
	Tile tiles[32 * MAX_PLAYERS + 4];
	uint32_t cursor_id;
	uint32_t num_players;
	uint32_t selected_id;
	uint32_t player_pov;
	uint32_t current_player;

	void initializeField();
	void initializeNeighborGraph();
	void createEdge(uint32_t a, uint32_t b);

	uint32_t calculateMoves(uint32_t start, bool mark_tiles);
	void moveFigure(MoveType move, uint32_t from, uint32_t to);

	bool isTileAttacked(uint32_t tile, uint32_t player, bool mark_attackers);
	bool isPlayerCheck(uint32_t player);
	bool isPlayerCheckMate(uint32_t player);

	template <typename F>
	void traverseReachableTiles(uint32_t start, Figure figure, F visitor) {
		bool is_on_opposing_half = tiles[start].player != getZ(start);

		#define isValidId(id) (id < num_players * 32)

		#define forward(pos) ({ \
			const uint64_t _pos = pos; \
			isValidId(extractId(_pos)) ? neighbors[extractId(_pos)][extractDirection(_pos)] : _pos; \
		})

		#define right(pos) ({ \
			const uint64_t _pos = pos; \
			isValidId(extractId(_pos)) ? neighbors[extractId(_pos)][(extractDirection(_pos) + 1) % 4] : _pos; \
		})

		#define left(pos) ({ \
			const uint64_t _pos = pos; \
			isValidId(extractId(_pos)) ? neighbors[extractId(_pos)][(extractDirection(_pos) + 3) % 4] : _pos; \
		})

		#define diagonalRight(pos) left(right(pos))
		#define diagonalLeft(pos) right(left(pos))

		if (figure == Figure::Pawn || figure == Figure::Any) {
			uint64_t current = forward(packIdAndDirection(start, is_on_opposing_half ? South : North));
			if (isValidId(extractId(current)) && tiles[extractId(current)].figure == Figure::None) {
				visitor(extractId(current), Figure::Pawn);
			}

			uint64_t left = left(current);
			if (isValidId(extractId(left)) && tiles[extractId(left)].figure != Figure::None) {
				visitor(extractId(left), Figure::Pawn);
			}

			left = diagonalLeft(packIdAndDirection(start, is_on_opposing_half ? South : North));
			if (isValidId(extractId(left)) && tiles[extractId(left)].figure != Figure::None) {
				visitor(extractId(left), Figure::Pawn);
			}

			uint64_t right = right(current);
			if (isValidId(extractId(right)) && tiles[extractId(right)].figure != Figure::None) {
				visitor(extractId(right), Figure::Pawn);
			}

			right = diagonalRight(packIdAndDirection(start, is_on_opposing_half ? South : North));
			if (isValidId(extractId(right)) && tiles[extractId(right)].figure != Figure::None) {
				visitor(extractId(right), Figure::Pawn);
			}

			if (tiles[start].move_count == 0 && isValidId(extractId(current)) && tiles[extractId(current)].figure == Figure::None) {
				current = forward(current);
				if (isValidId(extractId(current)) && tiles[extractId(current)].figure == Figure::None) {
					visitor(extractId(current), Figure::Pawn);
				}
			}
		}

		if (figure == Figure::Bishop || figure == Figure::Queen || figure == Figure::Any) {
			for (uint64_t d = North; d <= West; d++) {
				uint64_t current = diagonalRight(packIdAndDirection(start, d));
				for (uint64_t i = 0; i < 8; i++) {
					if (!isValidId(extractId(current))) {
						break;
					}
					visitor(extractId(current), Figure::Bishop);
					if (tiles[extractId(current)].figure != Figure::None) {
						break;
					}
					current = diagonalRight(current);
				}

				current = diagonalLeft(packIdAndDirection(start, d));
				for (uint64_t i = 0; i < 8; i++) {
					if (!isValidId(extractId(current))) {
						break;
					}
					visitor(extractId(current), Figure::Bishop);
					if (tiles[extractId(current)].figure != Figure::None) {
						break;
					}
					current = diagonalLeft(current);
				}
			}
		}

		if (figure == Figure::Knight || figure == Figure::Any) {
			for (uint64_t d = North; d <= West; d++) {
				const uint64_t current = packIdAndDirection(start, d);

				uint64_t tmp = extractId(right(forward(forward(current))));
				if (isValidId(tmp)) { visitor(tmp, Figure::Knight); }

				tmp = extractId(left(forward(forward(current))));
				if (isValidId(tmp)) { visitor(tmp, Figure::Knight); }

				tmp = extractId(forward(right(forward(current))));
				if (isValidId(tmp)) { visitor(tmp, Figure::Knight); }

				tmp = extractId(forward(left(forward(current))));
				if (isValidId(tmp)) { visitor(tmp, Figure::Knight); }
			}
		}

		if (figure == Figure::Rook || figure == Figure::Queen || figure == Figure::Any) {
			for (uint64_t d = North; d <= West; d++) {
				uint64_t current = forward(packIdAndDirection(start, d));
				while (isValidId(extractId(current))) {
					visitor(extractId(current), Figure::Rook);
					if (tiles[extractId(current)].figure != Figure::None) {
						break;
					}
					current = forward(current);
				}
			}
		}

		if (figure == Figure::King) {
			for (uint64_t d = North; d <= West; d++) {
				const uint64_t current = forward(packIdAndDirection(start, d));
				if (!isValidId(extractId(current))) {
					continue;
				}

				visitor(extractId(current), Figure::King);

				const uint32_t r = extractId(right(current));
				if (isValidId(r)) { visitor(r, Figure::King); }

				const uint32_t l = extractId(left(current));
				if (isValidId(l)) { visitor(l, Figure::King); }
			}
		}

		#undef isValidId
		#undef forward
		#undef right
		#undef left
		#undef diagonalRight
		#undef diagonalLeft
	}

	template <typename F>
	void traverseAttackingTiles(uint32_t tile, uint32_t player, F visitor) {
		traverseReachableTiles(tile, Figure::Any, [&](uint32_t id, Figure pattern) -> void {
			if (tiles[id].player == player || tiles[id].figure == Figure::None) {
				return;
			}

			if (tiles[id].figure == pattern) {
				visitor(id);
			} else if (tiles[id].figure == Figure::Queen && (pattern == Figure::Rook || pattern == Figure::Bishop)) {
				visitor(id);
			}
		});
	}
};

struct Message {
	enum : uint32_t {
		None,
		Move,
	} type;

	uint32_t player;
	union {
		struct {} none;

		struct {
			uint32_t from, to;
			MoveType type;
			Figure promotion;
		} move;
	};
};
