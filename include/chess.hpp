#pragma once

#include <cstdint>
#include <format>

#define getId(x, y, z) (((z)<<5) | ((y)<<3) | (x))
#define getX(id) ((id) & 0b00000111)
#define getY(id) (((id)>>3) & 0b00000011)
#define getXY(id) ((id) & 0b00011111)
#define getZ(id) ((id)>>5)

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

template <>
struct std::formatter<Figure> {
	constexpr auto parse(std::format_parse_context &ctx) {
		return ctx.begin();
	}

	auto format(const Figure& figure, std::format_context &ctx) const {
		switch (figure) {
			case Figure::None: return std::format_to(ctx.out(), "None");
			case Figure::Pawn: return std::format_to(ctx.out(), "Pawn");
			case Figure::Bishop: return std::format_to(ctx.out(), "Bishop");
			case Figure::Knight: return std::format_to(ctx.out(), "Knight");
			case Figure::Rook: return std::format_to(ctx.out(), "Rook");
			case Figure::Queen: return std::format_to(ctx.out(), "Queen");
			case Figure::King: return std::format_to(ctx.out(), "King");
			case Figure::Any: return std::format_to(ctx.out(), "Any");
			default: return std::format_to(ctx.out(), "");
		}
	}
};

enum class MoveType : uint8_t {
	None,
	Move,
	Capture,
	Castle,
	EnPassant,
};

template <>
struct std::formatter<MoveType> {
	constexpr auto parse(std::format_parse_context &ctx) {
		return ctx.begin();
	}

	auto format(const MoveType& move, std::format_context &ctx) const {
		switch (move) {
			case MoveType::None: return std::format_to(ctx.out(), "None");
			case MoveType::Move: return std::format_to(ctx.out(), "Move");
			case MoveType::Capture: return std::format_to(ctx.out(), "Capture");
			case MoveType::Castle: return std::format_to(ctx.out(), "Castle");
			case MoveType::EnPassant: return std::format_to(ctx.out(), "EnPassant");
			default: return std::format_to(ctx.out(), "");
		}
	}
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

struct IdAndDirection {
	uint32_t id: 30;
	uint32_t direction: 2;

	inline constexpr IdAndDirection() : id(UINT32_MAX>>2), direction(0) {}
	inline constexpr IdAndDirection(uint32_t id, uint32_t direction) : id(id), direction(direction) {}
};

struct Field {
	IdAndDirection neighbors[32 * MAX_PLAYERS][4];
	PlayerData players[MAX_PLAYERS];
	Tile tiles[32 * MAX_PLAYERS + 4];
	uint32_t num_players;
	uint32_t cursor_id;
	uint32_t selected_id;
	uint32_t player_pov;
	uint32_t current_player;

	inline explicit Field() {}

	void init(uint32_t num_players);
	void createEdge(uint32_t a, uint32_t b);

	uint32_t calculateMoves(uint32_t start, bool mark_tiles);
	void moveFigure(uint32_t from, uint32_t to, MoveType move);

	bool isTileAttacked(uint32_t tile, uint32_t player, bool mark_attackers);
	bool isPlayerCheck(uint32_t player);
	bool isPlayerCheckMate(uint32_t player);

	void switchToNextPlayer();

	template <typename F>
	void traverseReachableTiles(uint32_t start, Figure figure, F visitor) {
		bool is_on_opposing_half = tiles[start].player != getZ(start);

		#define isValidId(id) (id < num_players * 32)

		const auto move = [&](IdAndDirection pos, uint32_t rot) -> IdAndDirection {
			if (!isValidId(pos.id)) {
				return pos;
			}

			return neighbors[pos.id][(pos.direction + rot) % 4];
		};

		#define forward(pos) move(pos, 0)
		#define right(pos) move(pos, 1)
		#define left(pos) move(pos, 3)
		#define diagonalRight(pos) left(right(pos))
		#define diagonalLeft(pos) right(left(pos))

		if (figure == Figure::Pawn || figure == Figure::Any) {
			IdAndDirection current = forward(IdAndDirection(start, is_on_opposing_half ? South : North));
			if (isValidId(current.id) && tiles[current.id].figure == Figure::None) {
				visitor(current.id, Figure::Pawn);
			}

			IdAndDirection left = left(current);
			if (isValidId(left.id) && tiles[left.id].figure != Figure::None) {
				visitor(left.id, Figure::Pawn);
			}

			left = diagonalLeft(IdAndDirection(start, is_on_opposing_half ? South : North));
			if (isValidId(left.id) && tiles[left.id].figure != Figure::None) {
				visitor(left.id, Figure::Pawn);
			}

			IdAndDirection right = right(current);
			if (isValidId(right.id) && tiles[right.id].figure != Figure::None) {
				visitor(right.id, Figure::Pawn);
			}

			right = diagonalRight(IdAndDirection(start, is_on_opposing_half ? South : North));
			if (isValidId(right.id) && tiles[right.id].figure != Figure::None) {
				visitor(right.id, Figure::Pawn);
			}

			if (tiles[start].move_count == 0 && isValidId(current.id) && tiles[current.id].figure == Figure::None) {
				current = forward(current);
				if (isValidId(current.id) && tiles[current.id].figure == Figure::None) {
					visitor(current.id, Figure::Pawn);
				}
			}
		}

		if (figure == Figure::Bishop || figure == Figure::Queen || figure == Figure::Any) {
			for (uint32_t d = North; d <= West; d++) {
				IdAndDirection current = diagonalRight(IdAndDirection(start, d));
				for (uint32_t i = 0; i < 8; i++) {
					if (!isValidId(current.id)) {
						break;
					}
					visitor(current.id, Figure::Bishop);
					if (tiles[current.id].figure != Figure::None) {
						break;
					}
					current = diagonalRight(current);
				}

				current = diagonalLeft(IdAndDirection(start, d));
				for (uint32_t i = 0; i < 8; i++) {
					if (!isValidId(current.id)) {
						break;
					}
					visitor(current.id, Figure::Bishop);
					if (tiles[current.id].figure != Figure::None) {
						break;
					}
					current = diagonalLeft(current);
				}
			}
		}

		if (figure == Figure::Knight || figure == Figure::Any) {
			for (uint32_t d = North; d <= West; d++) {
				const IdAndDirection current = IdAndDirection(start, d);

				uint32_t tmp = right(forward(forward(current))).id;
				if (isValidId(tmp)) { visitor(tmp, Figure::Knight); }

				tmp = left(forward(forward(current))).id;
				if (isValidId(tmp)) { visitor(tmp, Figure::Knight); }

				tmp = forward(right(forward(current))).id;
				if (isValidId(tmp)) { visitor(tmp, Figure::Knight); }

				tmp = forward(left(forward(current))).id;
				if (isValidId(tmp)) { visitor(tmp, Figure::Knight); }
			}
		}

		if (figure == Figure::Rook || figure == Figure::Queen || figure == Figure::Any) {
			for (uint32_t d = North; d <= West; d++) {
				IdAndDirection current = forward(IdAndDirection(start, d));
				while (isValidId(current.id)) {
					visitor(current.id, Figure::Rook);
					if (tiles[current.id].figure != Figure::None) {
						break;
					}
					current = forward(current);
				}
			}
		}

		if (figure == Figure::King) {
			for (uint32_t d = North; d <= West; d++) {
				const IdAndDirection current = forward(IdAndDirection(start, d));
				if (!isValidId(current.id)) {
					continue;
				}

				visitor(current.id, Figure::King);

				const uint32_t r = right(current).id;
				if (isValidId(r)) { visitor(r, Figure::King); }

				const uint32_t l = left(current).id;
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
