#pragma once

#include "chess.hpp"

#include <cassert>
#include <cstdint>
#include <cstring>

struct Message {
	enum : uint32_t {
		None,
		Join,
		Accept,
		Reject,
		Move,
		Promotion,
	} type = None;

	uint32_t player;

	union {
		struct {
			uint32_t session;
			uint8_t name[20];
		} join;

		struct {
			uint32_t num_players;
		} accept;

		struct {
			uint32_t from, to;
			MoveType type;
			uint32_t next_player;
		} move;

		struct {
			uint32_t id;
			Figure figure;
			uint32_t next_player;
		} promotion;
	};

	static inline Message makeJoin(uint32_t session, uint32_t player, const std::string &name) {
		Message msg;
		msg.player = player;
		msg.type = Join;
		msg.join.session = session;

		memset(msg.join.name, 0, 16);
		assert(name.size() <= 16);
		memcpy(msg.join.name, name.data(), name.size());
		return msg;
	}

	static inline Message makeAccept(uint32_t player, uint32_t num_players) {
		Message msg;
		msg.type = Accept;
		msg.player = player;
		msg.accept.num_players = num_players;
		return msg;
	}

	static inline Message makeReject() {
		Message msg;
		msg.type = Reject;
		return msg;
	}

	static inline Message makeMove(uint32_t player, uint32_t from, uint32_t to, MoveType type) {
		Message msg;
		msg.type = Move;
		msg.player = player;
		msg.move.from = from;
		msg.move.to = to;
		msg.move.type = type;
		return msg;
	}

	static inline Message makePromotion(uint32_t player, uint32_t id, Figure figure) {
		Message msg;
		msg.type = Promotion;
		msg.player = player;
		msg.promotion.id = id;
		msg.promotion.figure = figure;
		return msg;
	}
};
