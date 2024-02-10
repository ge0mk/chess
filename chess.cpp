#include "chess.h"

void Field::initializeField() {
	cursor_id = 0;
	selected_id = 0;
	player_pov = 0;
	current_player = 0;

	for (uint32_t id = 0; id < MAX_PLAYERS * 32 + 4; id++) {
		tiles[id] = { Figure::None, 0, MoveType::None, 0 };
	}

	tiles[32 * MAX_PLAYERS + 0].figure = Figure::Bishop;
	tiles[32 * MAX_PLAYERS + 1].figure = Figure::Knight;
	tiles[32 * MAX_PLAYERS + 2].figure = Figure::Rook;
	tiles[32 * MAX_PLAYERS + 3].figure = Figure::Queen;

	for (uint32_t z = 0; z < num_players; z++) {
		for (uint32_t x = 0; x < 8; x++) {
			tiles[getId(x, 1, z)].figure = Figure::Pawn;
			tiles[getId(x, 1, z)].player = z;
			tiles[getId(x, 0, z)].player = z;
		}

		tiles[getId(0, 0, z)].figure = Figure::Rook;
		tiles[getId(1, 0, z)].figure = Figure::Knight;
		tiles[getId(2, 0, z)].figure = Figure::Bishop;
		tiles[getId(3, 0, z)].figure = Figure::Queen;
		tiles[getId(4, 0, z)].figure = Figure::King;
		tiles[getId(5, 0, z)].figure = Figure::Bishop;
		tiles[getId(6, 0, z)].figure = Figure::Knight;
		tiles[getId(7, 0, z)].figure = Figure::Rook;

		players[z].is_checkmate = false;
		players[z].king_position = getId(4, 0, z);
	}
}

void Field::initializeNeighborGraph() {
	for (uint32_t id = 0; id < num_players * 32; id++) {
		for (uint32_t dir = North; dir <= West; dir++) {
			neighbors[id][dir] = IdAndDirection();
		}
	}

	for (uint32_t z = 0; z < num_players; z++) {
		for (uint32_t y = 0; y < 3; y++) {
			for (uint32_t x = 0; x < 7; x++) {
				createEdge(getId(x, y, z), getId(x + 1, y, z));
				createEdge(getId(x, y, z), getId(x, y + 1, z));
			}
		}

		for (uint32_t x = 0; x < 7; x++) {
			createEdge(getId(x, 3, z), getId(x + 1, 3, z));
		}

		for (uint32_t y = 0; y < 3; y++) {
			createEdge(getId(7, y, z), getId(7, y + 1, z));
		}

		for (uint32_t x = 0; x < 4; x++) {
			createEdge(getId(x, 3, z), getId(7 - x, 3, (z + 1) % num_players));
		}
	}
}

void Field::createEdge(uint32_t a, uint32_t b) {
	if (getZ(a) != getZ(b)) {
		neighbors[a][North] = IdAndDirection(b, South);
		neighbors[b][North] = IdAndDirection(a, South);
	} else if (getX(a) < getX(b)) {
		neighbors[a][West] = IdAndDirection(b, West);
		neighbors[b][East] = IdAndDirection(a, East);
	} else {// (getY(a) < getY(b)) {
		neighbors[a][North] = IdAndDirection(b, North);
		neighbors[b][South] = IdAndDirection(a, South);
	}
}

uint32_t Field::calculateMoves(uint32_t start, bool mark_tiles) {
	uint32_t num_reachable_tiles = 0;
	traverseReachableTiles(start, tiles[start].figure, [&](uint32_t id, Figure) {
		if (tiles[start].figure == Figure::King && isTileAttacked(id, tiles[start].player, false)) {
			return;
		}

		if (tiles[id].figure == Figure::None) {
			if (mark_tiles) {
				tiles[id].move = MoveType::Move;
			}
			num_reachable_tiles++;
		} else if (tiles[id].player != tiles[start].player) {
			if (mark_tiles) {
				tiles[id].move = MoveType::Capture;
			}
			num_reachable_tiles++;
		}
	});

	if (tiles[start].figure == Figure::King && tiles[start].move_count == 0 && !isTileAttacked(start, tiles[start].player, false)) {
		const uint32_t left_rook_id = getId(0, 0, getZ(start));
		const uint32_t right_rook_id = getId(7, 0, getZ(start));

		if (tiles[left_rook_id].move_count == 0) {
			bool is_valid_for_casteling = true;

			// king must not pass through or end up on an attacked tile
			is_valid_for_casteling &= !isTileAttacked(start - 1, tiles[start].player, false);
			is_valid_for_casteling &= !isTileAttacked(start - 2, tiles[start].player, false);

			// all tiles between king & rook must be empty
			for (uint32_t x = 1; x < getX(start); x++) {
				is_valid_for_casteling &= tiles[getId(x, 0, getZ(start))].figure == Figure::None;
			}

			if (is_valid_for_casteling) {
				if (mark_tiles) {
					tiles[left_rook_id].move = MoveType::Castle;
				}
				num_reachable_tiles++;
			}
		}

		if (tiles[right_rook_id].move_count == 0) {
			bool is_valid_for_casteling = true;

			// king must not pass through or end up on an attacked tile
			is_valid_for_casteling &= !isTileAttacked(start + 1, tiles[start].player, false);
			is_valid_for_casteling &= !isTileAttacked(start + 2, tiles[start].player, false);

			// all tiles between king & rook must be empty
			for (uint32_t x = 6; x > getX(start); x--) {
				is_valid_for_casteling &= tiles[getId(x, 0, getZ(start))].figure == Figure::None;
			}

			if (is_valid_for_casteling) {
				if (mark_tiles) {
					tiles[right_rook_id].move = MoveType::Castle;
				}
				num_reachable_tiles++;
			}
		}
	}

	return num_reachable_tiles;
}

void Field::moveFigure(MoveType move, uint32_t from, uint32_t to) {
	switch (move) {
		case MoveType::None: return;
		case MoveType::Move:
		case MoveType::Capture: {
			tiles[to].figure = tiles[from].figure;
			tiles[to].player = tiles[from].player;
			tiles[to].move_count = tiles[from].move_count + 1;
			tiles[from].figure = Figure::None;

			if (tiles[to].figure == Figure::King) {
				players[tiles[to].player].king_position = to;
			}
		} break;
		case MoveType::Castle: {
			uint32_t king_dst;
			uint32_t rook_dst;
			if (getX(from) < getX(to)) {
				king_dst = getId(getX(from) + 2, 0, getZ(from));
				rook_dst = getId(getX(from) + 1, 0, getZ(from));
			} else {
				king_dst = getId(getX(from) - 2, 0, getZ(from));
				rook_dst = getId(getX(from) - 1, 0, getZ(from));
			}

			tiles[king_dst].figure = tiles[from].figure;
			tiles[king_dst].player = tiles[from].player;
			tiles[king_dst].move_count = tiles[from].move_count + 1;
			tiles[from].figure = Figure::None;

			tiles[rook_dst].figure = tiles[to].figure;
			tiles[rook_dst].player = tiles[to].player;
			tiles[rook_dst].move_count = tiles[to].move_count + 1;
			tiles[to].figure = Figure::None;

			players[tiles[king_dst].player].king_position = king_dst;
		} break;
		case MoveType::EnPassant: {} break;
	}
}

bool Field::isTileAttacked(uint32_t tile, uint32_t player, bool mark_attackers) {
	bool is_attacked = false;

	traverseAttackingTiles(tile, player, [&](uint32_t id) -> void {
		is_attacked = true;

		if (mark_attackers) {
			tiles[id].move = MoveType::Capture;
		}
	});

	return is_attacked;
}

bool Field::isPlayerCheck(uint32_t player) {
	return isTileAttacked(players[player].king_position, player, false);
}

bool Field::isPlayerCheckMate(uint32_t player) {
	// TODO: check if player can capture / block the attacking figure
	return isPlayerCheck(player) && calculateMoves(players[player].king_position, false) == 0;
}
