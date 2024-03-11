#include "session.hpp"

#include "SDL3_net/SDL_net.h"
#include "SDL_error.h"
#include "chess.hpp"
#include "io.hpp"
#include "message.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

Session::Session() {}

Session::~Session() {
	deinit();
}

void Session::initLocal(uint32_t num_players) {
	mode = Mode::Local;
	initializeField(num_players);
}

void Session::initClient(const std::string &hostname, uint16_t port, const std::string &player_name) {
	mode = Mode::Client;
	connectToServer(hostname, port);
	sendMessageToServer(Message::makeJoin(0, -1, player_name));
}

void Session::initHost(uint32_t num_players, std::optional<uint16_t> port) {
	mode = Mode::Host;
	initializeField(num_players);
	if (port) {
		server = SDLNet_CreateServer(nullptr, port.value());
	}
}

void Session::initHostHybrid(uint32_t num_players, std::optional<uint16_t> port, const std::string &player_name) {
	mode = Mode::HostHybrid;
	initializeField(num_players);
	if (port) {
		server = SDLNet_CreateServer(nullptr, port.value());
	}

	players[0] = Player(player_name, true);
}

void Session::launchThread() {
	thread = std::thread([this](){
		while (!stop) {
			acceptQueuedPlayers();
			waitForMessagesFromClients();
			receiveMessagesFromClients();
		}
	});
}

void Session::initializeField(uint32_t num_players) {
	field.init(num_players);
	onFieldInitialized();
	players.resize(num_players);
}

void Session::deinit() {
	if (server) {
		SDLNet_DestroyServer(server);
		server = nullptr;
	}

	if (socket) {
		SDLNet_DestroyStreamSocket(socket);
		socket = nullptr;
	}

	if (mode & Mode::Host) {
		for (SDLNet_StreamSocket *socket : clients) {
			SDLNet_DestroyStreamSocket(socket);
		}
		clients.clear();

		std::scoped_lock<std::mutex> queue_lock{queue_mutex};
		queue.clear();
	}

	players.clear();

	mode = Mode::None;
}

void Session::moveFigure(uint32_t from, uint32_t to, MoveType type) {
	const uint32_t player = field.current_player;

	if (mode == Mode::Client) {
		sendMessageToServer(Message::makeMove(player, from, to, type));
	} else {
		field.moveFigure(from, to, type);
		onFigureMoved(player, from, to, type);

		if (field.tiles[to].figure != Figure::Pawn || getY(to) != 0) {
			switchToNextPlayer();
		}

		if (mode & Mode::Host) {
			Message msg = Message::makeMove(player, from, to, type);
			msg.move.next_player = field.current_player;
			sendMessageToAllClients(msg);
		}
	}
}

void Session::promoteFigure(uint32_t id, Figure to) {
	const uint32_t player = field.current_player;

	if (mode == Mode::Client) {
		sendMessageToServer(Message::makePromotion(player, id, to));
	} else {
		if (field.tiles[id].figure != Figure::Pawn || getY(id) != 0) {
			return;
		}

		field.tiles[id].figure = to;
		onFigurePromoted(player, id, to);
		switchToNextPlayer();

		if (mode & Mode::Host) {
			Message msg = Message::makePromotion(player, id, to);
			msg.promotion.next_player = field.current_player;
			sendMessageToAllClients(msg);
		}
	}
}

void Session::switchToNextPlayer() {
	field.switchToNextPlayer();
	if (mode == Mode::Local) {
		field.player_pov = field.current_player;
	}
}

void Session::disconnectClient(uint64_t player) {
	assert(mode & Mode::Host);

	println("client ({}) disconnected\n",
		SDLNet_GetAddressString(SDLNet_GetStreamSocketAddress(players[player].socket))
	);

	SDLNet_DestroyStreamSocket(players[player].socket);
	clients.erase(std::find(clients.begin(), clients.end(), players[player].socket));
	players[player].socket = nullptr;
}

void Session::waitForMessagesFromClients(int timeout) {
	SDLNet_WaitUntilInputAvailable((void**)clients.data(), clients.size(), timeout);
}

void Session::receiveMessagesFromClients() {
	assert(mode & Mode::Host);

	for (size_t i = 0; i < players.size(); i++) {
		if (players[i].socket == nullptr) {
			continue;
		}

		Message msg;
		const int size = SDLNet_ReadFromStreamSocket(players[i].socket, &msg, sizeof(Message));
		if (size == 0) {
			continue;
		} else if (size < 0) {
			println("error while receiving message from client {}[{}], disconnecting ...",
				players[i].name,
				SDLNet_GetAddressString(SDLNet_GetStreamSocketAddress(players[i].socket))
			);
			disconnectClient(i);
			continue;
		} else if (size != sizeof(Message)) {
			println("received incomplete message from client {}[{}], disconnecting ...",
				players[i].name,
				SDLNet_GetAddressString(SDLNet_GetStreamSocketAddress(players[i].socket))
			);
			disconnectClient(i);
			continue;
		}

		handleMessageFromClient(i, msg);
	}
}

void Session::handleMessageFromClient(uint64_t player, Message msg) {
	switch (msg.type) {
		case Message::None:
		case Message::Join:
		case Message::Accept:
		case Message::Reject: {} break;

		case Message::Move: {
			if (field.current_player != player) {
				break;
			}

			field.moveFigure(msg.move.from, msg.move.to, msg.move.type);
			onFigureMoved(msg.player, msg.move.from, msg.move.to, msg.move.type);

			if (field.tiles[msg.move.to].figure != Figure::Pawn || getY(msg.move.to) != 0) {
				field.switchToNextPlayer();
			}

			msg.move.next_player = field.current_player;
			sendMessageToAllClients(msg);
		} break;
		case Message::Promotion: {
			if (field.current_player != player) {
				break;
			}

			if (field.tiles[msg.promotion.id].figure == Figure::Pawn && getY(msg.promotion.id) == 0) {
				field.tiles[msg.promotion.id].figure = msg.promotion.figure;
			}

			onFigurePromoted(msg.player, msg.promotion.id, msg.promotion.figure);

			field.switchToNextPlayer();
			msg.promotion.next_player = field.current_player;
			sendMessageToAllClients(msg);
		} break;
	}
}

void Session::sendMessageToAllClients(const Message &msg) {
	assert(mode & Mode::Host);

	for (size_t i = 0; i < players.size(); i++) {
		if (players[i].socket == nullptr) {
			continue;
		}

		if (SDLNet_WriteToStreamSocket(players[i].socket, &msg, sizeof(Message)) != 0) {
			println("error while sending to client {}, disconnecting ...",
				SDLNet_GetAddressString(SDLNet_GetStreamSocketAddress(players[i].socket))
			);
			disconnectClient(i);
		}
	}
}

void Session::sendMessageToClient(uint64_t index, const Message &msg) {
	assert(mode & Mode::Host);

	if (players[index].socket == nullptr) {
		return;
	}

	if (SDLNet_WriteToStreamSocket(players[index].socket, &msg, sizeof(Message)) != 0) {
		println("error while sending to client {}({}), disconnecting ...", players[index].name, players[index].getAddress());
		disconnectClient(index);
	}
}

void Session::acceptQueuedPlayers() {
	assert(mode & Mode::Host);

	std::scoped_lock<std::mutex> queue_lock{queue_mutex};

	if (server) {
		Player player;
		if (SDLNet_AcceptClient(server, &player.socket) != 0) {
			eprintln("couldn't accept client: {}\n", SDL_GetError());
		} else if (player.socket) {
			Message msg;
			if (receiveBlocking<Message>(player.socket, &msg, 1) == 0) {
				assert(msg.type == Message::Join);
				std::string tmp(sizeof(msg.join.name) + 1, '\0');
				memcpy(tmp.data(), msg.join.name, sizeof(msg.join.name));
				player.name = tmp.c_str();

				const uint32_t index = msg.player;
				queue.push_back({player, index});

				println("client {}({}) added to queue as player {}", player.name, player.getAddress(), index);
			} else {
				println("client {} didn't send a join request after connecting", player.getAddress());
			}
		}
	}

	if (!queue.empty()) {
		for (auto [player, index] : queue) {
			if (index == ~0u) {
				for (size_t i = 0; i < players.size(); i++) {
					if (players[i].socket == nullptr && !players[i].is_host) {
						index = i;
						break;
					}
				}
			}

			if (index >= players.size()) {
				println("client {}({}) wants to join invalid spot {}", player.name, player.getAddress(), index);
				const Message msg = Message::makeReject();
				SDLNet_WriteToStreamSocket(player.socket, &msg, sizeof(Message));
				SDLNet_DestroyStreamSocket(player.socket);
				continue;
			}

			if (players[index].socket != nullptr || players[index].is_host) {
				println("client {}({}) wants to join already occupied spot {}({})", player.name, player.getAddress(), index, players[index].name);
				const Message msg = Message::makeReject();
				SDLNet_WriteToStreamSocket(player.socket, &msg, sizeof(Message));
				SDLNet_DestroyStreamSocket(player.socket);
				continue;
			}

			players[index] = player;
			clients.push_back(player.socket);

			sendMessageToClient(index, Message::makeAccept(index, field.num_players));
			println("accepted client {}({}) as player {}, sending match status", player.name, player.getAddress(), index);

			SDLNet_WriteToStreamSocket(player.socket, field.tiles, sizeof(Tile) * 32 * field.num_players);
			println("sent field to client {}({})", player.name, player.getAddress());

			for (size_t i = 0; i < players.size(); i++) {
				if (i == index) {
					continue;
				} else if (players[i].socket == nullptr && !players[i].is_host) {
					continue;
				}

				sendMessageToClient(index, Message::makeJoin(0, i, players[i].name));
			}

			sendMessageToAllClients(Message::makeJoin(0, index, player.name));
		}

		queue.clear();
	}
}

void Session::addClientToQueue(Player player, uint64_t index) {
	assert(mode & Mode::Host);

	std::scoped_lock<std::mutex> queue_lock{queue_mutex};
	queue.push_back({player, index});
}

void Session::connectToServer(const std::string &hostname, uint16_t port) {
	assert(hostname.c_str()[hostname.size()] == '\0');
	SDLNet_Address *addr = SDLNet_ResolveHostname(hostname.c_str());
	if (!addr) {
		eprintln("Couldn't resolve hostname ({}): {}\n", hostname, SDL_GetError());
		return;
	}

	if (SDLNet_WaitUntilResolved(addr, -1) != 1) {
		eprintln("Couldn't resolve hostname ({}): {}\n", hostname, SDL_GetError());
		SDLNet_UnrefAddress(addr);
		return;
	}

	socket = SDLNet_CreateClient(addr, port);
	if (SDLNet_WaitUntilConnected(socket, -1) != 1) {
		eprintln("Couldn't connect to server ({}): {}\n", SDLNet_GetAddressString(addr), SDL_GetError());
		SDLNet_UnrefAddress(addr);
		socket = nullptr;
		return;
	}

	SDLNet_UnrefAddress(addr);
}

void Session::disconnectFromServer() {
	deinit();
}

void Session::sendMessageToServer(const Message &msg) {
	SDLNet_WriteToStreamSocket(socket, &msg, sizeof(Message));
}

void Session::receiveMessageFromServer() {
	Message msg;
	int received = SDLNet_ReadFromStreamSocket(socket, &msg, sizeof(Message));

	if (received == 0) {
		return;
	} else if (received != sizeof(Message)) {
		println("received incomplete message from server");
		disconnectFromServer();
		return;
	}

	switch (msg.type) {
		case None: break;
		case Message::Join: {
			std::string tmp(sizeof(msg.join.name) + 1, '\0');
			memcpy(tmp.data(), msg.join.name, sizeof(msg.join.name));
			players[msg.player].name = tmp.c_str();
		} break;
		case Message::Accept: {
			initializeField(msg.accept.num_players);
			field.player_pov = msg.player;

			println("joined server as player {}, receiving field ...", msg.player);
			if (receiveBlocking(socket, field.tiles, 32 * field.num_players) != 0) {
				println("received incomplete field from server, disconnecting ...");
				disconnectFromServer();
			} else {
				println("received field from server, ready to play");
			}
		} break;
		case Message::Reject: {
			disconnectFromServer();
		} break;
		case Message::Move: {
			field.current_player = msg.move.next_player;
			field.moveFigure(msg.move.from, msg.move.to, msg.move.type);
			onFigureMoved(msg.player, msg.move.from, msg.move.to, msg.move.type);
		} break;
		case Message::Promotion: {
			field.current_player = msg.promotion.next_player;
			field.tiles[msg.promotion.id].figure = msg.promotion.figure;
			onFigurePromoted(msg.player, msg.promotion.id, msg.promotion.figure);
		} break;
	}
}

void Session::onFieldInitialized() {}

void Session::onGameBegin() {}

void Session::onGameEnd() {}

void Session::onTurnBegin() {}

void Session::onTurnEnd() {}

void Session::onFigureMoved(uint32_t player, uint32_t from, uint32_t to, MoveType type) {
	switch (type) {
		case MoveType::None: break;
		case MoveType::Move: {
			log.push_back(Event(player, from, to, Figure::None, Event::Kind::Move));
		} break;
		case MoveType::Capture: {
			log.push_back(Event(player, from, to, Figure::None, Event::Kind::Capture));
		} break;
		case MoveType::Castle: {
			log.push_back(Event(player, from, to, Figure::None, Event::Kind::Castle));
		} break;
		case MoveType::EnPassant: {
			log.push_back(Event(player, from, to, Figure::None, Event::Kind::EnPassant));
		} break;
	}
}

void Session::onFigurePromoted(uint32_t player, uint32_t id, Figure to) {
	log.push_back(Event(player, id, 0, to, Event::Kind::Promote));
}

void Session::onCheck(uint32_t player) {
	log.push_back(Event(player, 0, 0, Figure::None, Event::Kind::Check));
}

void Session::onCheckMate(uint32_t player) {
	log.push_back(Event(player, 0, 0, Figure::None, Event::Kind::CheckMate));
}
