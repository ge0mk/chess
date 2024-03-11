#pragma once

#include "chess.hpp"
#include "message.hpp"

#include "SDL3_net/SDL_net.h"

#include <cstdint>
#include <thread>
#include <vector>

struct Player {
	std::string name;
	SDLNet_StreamSocket *socket = nullptr;
	bool is_host = false;

	inline Player() {}
	inline explicit Player(const std::string &name) : name(name) {}
	inline Player(const std::string &name, bool is_host) : name(name), is_host(is_host) {}

	inline std::string getAddress() const {
		if (socket) {
			return SDLNet_GetAddressString(SDLNet_GetStreamSocketAddress(socket));
		} else {
			return "<null>";
		}
	}
};

class Session {
public:
	enum Mode {
		None = 0,
		Local = 1,
		Client = 2,
		Host = 4,
		HostHybrid = Client | Host,
	} mode = None;

	explicit Session();
	virtual ~Session();

	void initLocal(uint32_t num_players);
	void initClient(const std::string &hostname, uint16_t port, const std::string &player_name);
	void initHost(uint32_t num_players, std::optional<uint16_t> port);
	void initHostHybrid(uint32_t num_players, std::optional<uint16_t> port, const std::string &player_name);
	void launchThread();

	void initializeField(uint32_t num_players);

	void deinit();

	void update();

	// chess actions
	void moveFigure(uint32_t id, uint32_t dst, MoveType type);
	void promoteFigure(uint32_t id, Figure to);
	void switchToNextPlayer();

	// network host mode
	void disconnectClient(uint64_t player);
	void waitForMessagesFromClients(int timeout = -1);
	void receiveMessagesFromClients();
	void handleMessageFromClient(uint64_t index, Message msg);
	void sendMessageToAllClients(const Message &msg);
	void sendMessageToClient(uint64_t index, const Message &msg);
	void acceptQueuedPlayers();
	void addClientToQueue(Player player, uint64_t index);

	// network client mode
	void connectToServer(const std::string &hostname, uint16_t port);
	void disconnectFromServer();
	void sendMessageToServer(const Message &msg);
	void receiveMessageFromServer();
	void handleMessageFromServer(const Message &msg);

	template <typename T>
	static int receiveBlocking(SDLNet_StreamSocket *socket, T *result, size_t count) {
		uint8_t *dst = reinterpret_cast<uint8_t*>(result);
		const size_t total = count * sizeof(T);
		size_t received = 0;

		int chunk = SDLNet_ReadFromStreamSocket(socket, dst, total - received);
		if (chunk < 0) {
			return -1;
		} else {
			received += chunk;
		}

		while (received < total) {
			if (SDLNet_WaitUntilInputAvailable((void**)&socket, 1, 100) != 1) {
				return -1;
			}

			chunk = SDLNet_ReadFromStreamSocket(socket, dst, total - received);
			if (chunk <= 0) {
				return -1;
			} else {
				received += chunk;
			}
		}

		return 0;
	}

	// events
	virtual void onFieldInitialized();
	virtual void onGameBegin();
	virtual void onGameEnd();
	virtual void onTurnBegin();
	virtual void onTurnEnd();
	virtual void onFigureMoved(uint32_t player, uint32_t from, uint32_t to, MoveType type);
	virtual void onFigurePromoted(uint32_t player, uint32_t id, Figure to);
	virtual void onCheck(uint32_t player);
	virtual void onCheckMate(uint32_t player);

protected:
	Field field;

	std::thread thread;
	std::atomic<bool> stop = false;

	std::vector<Player> players;

	struct Event {
		uint32_t player;
		uint32_t from;
		uint32_t to;
		Figure promotion;

		enum Kind: uint8_t {
			Move,
			Capture,
			Castle,
			EnPassant,
			Promote,
			Check,
			CheckMate,
			Surrender,
		} kind;
	};

	std::vector<Event> log;

	// network host mode
	std::mutex queue_mutex;
	std::vector<std::pair<Player, uint32_t>> queue;
	std::vector<SDLNet_StreamSocket*> clients;
	SDLNet_Server *server = nullptr;

	// network client mode
	SDLNet_StreamSocket *socket = nullptr;
};
