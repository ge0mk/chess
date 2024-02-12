#include "SDL_oldnames.h"
#include "SDL_video.h"
#include "chess.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <iostream>
#include <iterator>
#include <fstream>
#include <span>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_surface.h>
#include <SDL3_net/SDL_net.h>
#include <SDL3_image/SDL_image.h>

#include "glad.h"

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_sdl3.h"
#include "imgui/backends/imgui_impl_opengl3.h"

#define PI 3.141592654f

#define panic(format, ...) (fprintf(stderr, format, ##__VA_ARGS__), abort())

enum ShaderSource {
	ShaderSource_File,
	ShaderSource_Memory,
};

enum ShaderFormat {
	ShaderFormat_GLSL,
	ShaderFormat_SPIRV,
};

struct Vertex {
	float x, y;
	float u, v;
	uint32_t id;

	inline constexpr Vertex(float x, float y, float u, float v, uint32_t id) : x(x), y(y), u(u), v(v), id(id) {}
};

struct VertexBuffer {
	GLuint handle;
	GLuint vao;
	uint64_t vertex_count;
};

struct ViewportInfoUniformData {
	float x_offset;
	float y_offset;
	float scale;
	float aspect_ratio;
};

GLuint loadShader(std::span<uint8_t> source, GLenum type, ShaderFormat format) {
	if (source.size() == 0) {
		return -1;
	}

	GLuint shader = glCreateShader(type);

	switch (format) {
		case ShaderFormat_GLSL: {
			const uint8_t *tmp = source.data();
			glShaderSource(shader, 1, (const char**)&tmp, NULL);
			glCompileShader(shader);
		} break;
		case ShaderFormat_SPIRV: {
			glShaderBinary(1, &shader, GL_SHADER_BINARY_FORMAT_SPIR_V_ARB, source.data(), source.size());
			glSpecializeShader(shader, "main", 0, NULL, NULL);
		} break;
	}

	int success;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

	if(!success) {
		char info_log[2048];
		glGetShaderInfoLog(shader, 2048, NULL, info_log);
		panic("Failed to compile shader: \n%s\n", info_log);
	}

	return shader;
}

GLuint loadProgram(uint64_t num_shaders, ...) {
	const GLuint program = glCreateProgram();
	GLuint shaders[10];

	va_list ptr;
	va_start(ptr, num_shaders);
	for (uint64_t i = 0; i < num_shaders; i++) {
		const GLenum type = va_arg(ptr, GLenum);
		const ShaderFormat format = static_cast<ShaderFormat>(va_arg(ptr, int));
		const ShaderSource source = static_cast<ShaderSource>(va_arg(ptr, int));
		const char *path_or_data = va_arg(ptr, const char*);

		if (source == ShaderSource_File) {
			std::ifstream file(path_or_data);
			std::string content{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
			shaders[i] = loadShader(std::span<uint8_t>((uint8_t*)content.c_str(), content.size()), type, format);
		} else if (source == ShaderSource_Memory) {
			const uint64_t size = va_arg(ptr, uint64_t);
			shaders[i] = loadShader(std::span<uint8_t>((uint8_t*)path_or_data, size), type, format);
		}

		glAttachShader(program, shaders[i]);
	}
	va_end(ptr);

	glLinkProgram(program);

	for (uint64_t i = 0; i < num_shaders; i++) {
		glDeleteShader(shaders[i]);
	}

	int success;
	glGetProgramiv(program, GL_LINK_STATUS, &success);
	if(!success) {
		char info_log[2048];
		glGetProgramInfoLog(program, 2048, NULL, info_log);
		panic("Failed to link shader program: \n%s", info_log);
	}

	return program;
}

class Game {
public:
	Game(float width, float height) : width(width), height(height) {
		field.num_players = 3;
		field.initializeNeighborGraph();
		field.initializeField();

		SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);

		SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
		SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

		window = SDL_CreateWindow("client", width, height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);
		if (!window) {
			panic("Failed to create a window %s\n", SDL_GetError());
		}

		gl_context = SDL_GL_CreateContext(window);
		SDL_GL_MakeCurrent(window, gl_context);
		SDL_GL_SetSwapInterval(1);
		SDL_ShowWindow(window);

		gladLoadGLLoader((void*(*)(const char*))SDL_GL_GetProcAddress);

		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glClearColor(0.2f, 0.2f, 0.2f, 0.0f);

		viewport_info_uniform_data.scale = 0.2f;
		viewport_info_uniform_data.x_offset = 0.0f;
		viewport_info_uniform_data.y_offset = 0.0f;

		loadShaders();
		loadTextures();
		createVertexBuffers();
		createUniformBuffers();

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO(); (void)io;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

		ImGui::StyleColorsDark();

		ImGui_ImplSDL3_InitForOpenGL(window, gl_context);
		ImGui_ImplOpenGL3_Init("#version 460");

		connectToServer(server_address, server_port);
	}

	~Game() {
		if (socket) {
			SDLNet_DestroyStreamSocket(socket);
		}

		SDL_GL_DeleteContext(gl_context);
		SDL_DestroyWindow(window);
	}

	void loadShaders() {
		field_shader = loadProgram(2,
	#if defined(SPIRV_SHADERS)
			GL_VERTEX_SHADER, ShaderFormat_SPIRV, ShaderSource_File, "shaders/field.vert.spv",
			GL_FRAGMENT_SHADER, ShaderFormat_SPIRV, ShaderSource_File, "shaders/field.frag.spv"
	#else
			GL_VERTEX_SHADER, ShaderFormat_GLSL, ShaderSource_File, "shaders/field.vert",
			GL_FRAGMENT_SHADER, ShaderFormat_GLSL, ShaderSource_File, "shaders/field.frag"
	#endif
		);
	}

	void createUniformBuffers() {
		viewport_info_uniform_data.aspect_ratio = width / height;

		glGenBuffers(1, &viewport_info_uniform_buffer);
		glBindBuffer(GL_UNIFORM_BUFFER, viewport_info_uniform_buffer);
		glBufferData(GL_UNIFORM_BUFFER, sizeof(ViewportInfoUniformData), &viewport_info_uniform_data, GL_DYNAMIC_DRAW);

		glGenBuffers(1, &field_uniform_buffer);
		glBindBuffer(GL_UNIFORM_BUFFER, field_uniform_buffer);
		glBufferData(GL_UNIFORM_BUFFER, sizeof(field.tiles) + sizeof(uint32_t) * 5, &field.tiles, GL_DYNAMIC_DRAW);

		glGenBuffers(1, &time_uniform_buffer);
		glBindBuffer(GL_UNIFORM_BUFFER, time_uniform_buffer);
		glBufferData(GL_UNIFORM_BUFFER, sizeof(float), NULL, GL_DYNAMIC_DRAW);
	}

	void loadTextures() {
		glGenTextures(1, &palette_texture);
		glBindTexture(GL_TEXTURE_2D, palette_texture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

		SDL_Surface *palette = IMG_Load("palette.png");
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, palette->w, palette->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, palette->pixels);
		SDL_DestroySurface(palette);

		glGenerateMipmap(GL_TEXTURE_2D);

		glGenTextures(1, &spritesheet_texture);
		glBindTexture(GL_TEXTURE_2D, spritesheet_texture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		SDL_Surface *spritesheet = IMG_Load("spritesheet.png");
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, spritesheet->w, spritesheet->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, spritesheet->pixels);
		SDL_DestroySurface(spritesheet);

		glGenerateMipmap(GL_TEXTURE_2D);
	}

	void createVertexBuffers() {
		glGenVertexArrays(1, &field_mesh.vao);
		glBindVertexArray(field_mesh.vao);

		glGenBuffers(1, &field_mesh.handle);
		glBindBuffer(GL_ARRAY_BUFFER, field_mesh.handle);

		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), 0);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, u));
		glEnableVertexAttribArray(1);
		glVertexAttribIPointer(2, 1, GL_INT, sizeof(Vertex), (void*)offsetof(Vertex, id));
		glEnableVertexAttribArray(2);


		glGenVertexArrays(1, &field_mesh.vao);
		glBindVertexArray(field_mesh.vao);

		glGenBuffers(1, &field_mesh.handle);
		glBindBuffer(GL_ARRAY_BUFFER, field_mesh.handle);

		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), 0);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, u));
		glEnableVertexAttribArray(1);
		glVertexAttribIPointer(2, 1, GL_INT, sizeof(Vertex), (void*)offsetof(Vertex, id));
		glEnableVertexAttribArray(2);

		updateVertexBufferData();
	}

	void updateVertexBufferData() {
		field_mesh.vertex_count = 0;
		field_mesh.vertex_count = 0;

		const uint64_t vertex_count = (field.num_players * 32 * 2 + 4) * 6;
		Vertex *vertices = (Vertex*)malloc(vertex_count * sizeof(Vertex));

		if (!vertices) {
			panic("out of memory\n");
		}

		for (uint32_t half_segment = 0; half_segment < field.num_players * 2; half_segment++) {
			const float angle_a = -((half_segment + 0.0f) / (field.num_players * 2.0f) * 2.0f * PI + PI / (1.0f + 1.0f / (field.num_players - 1.0f)));
			const float angle_b = -((half_segment + 1.0f) / (field.num_players * 2.0f) * 2.0f * PI + PI / (1.0f + 1.0f / (field.num_players - 1.0f)));

			const float delta_a_x = -sinf(angle_a);
			const float delta_a_y = +cosf(angle_a);

			const float delta_b_x = -sinf(angle_b);
			const float delta_b_y = +cosf(angle_b);

			for (uint32_t a = 0; a < 4; a++) {
				for (uint32_t b = 0; b < 4; b++) {
					const uint32_t field_x = (half_segment % 2) ? (3 - b) : a + 4;
					const uint32_t field_y = 3 - ((half_segment % 2) ? a : b);
					const uint32_t field_z = half_segment >> 1;

					const float x0 = delta_a_x * (a + 0) + delta_b_x * (b + 0);
					const float y0 = delta_a_y * (a + 0) + delta_b_y * (b + 0);

					const float x1 = delta_a_x * (a + 1) + delta_b_x * (b + 0);
					const float y1 = delta_a_y * (a + 1) + delta_b_y * (b + 0);

					const float x2 = delta_a_x * (a + 0) + delta_b_x * (b + 1);
					const float y2 = delta_a_y * (a + 0) + delta_b_y * (b + 1);

					const float x3 = delta_a_x * (a + 1) + delta_b_x * (b + 1);
					const float y3 = delta_a_y * (a + 1) + delta_b_y * (b + 1);

					const float x_center = (x0 + x3) / 2.0f;
					const float y_center = (y0 + y3) / 2.0f;

					#define addFieldVertex(X, Y, U, V) \
						vertices[field_mesh.vertex_count++] = Vertex( \
							X, Y, U, V, getId(field_x, field_y, field_z) | 0x200 \
						)

					addFieldVertex(x1, y1, 1, 0);
					addFieldVertex(x0, y0, 0, 0);
					addFieldVertex(x2, y2, 0, 1);

					addFieldVertex(x2, y2, 0, 1);
					addFieldVertex(x3, y3, 1, 1);
					addFieldVertex(x1, y1, 1, 0);

					#undef addFieldVertex

					#define addPieceVertex(X, Y, U, V) \
						vertices[field_mesh.vertex_count++] = Vertex( \
							X + (U - 0.5f) / 2, Y + (V - 0.5f) / 2, U, 1 - V, \
							getId(field_x, field_y, field_z) \
						)

					addPieceVertex(x_center, y_center, 1, 0);
					addPieceVertex(x_center, y_center, 0, 0);
					addPieceVertex(x_center, y_center, 0, 1);

					addPieceVertex(x_center, y_center, 0, 1);
					addPieceVertex(x_center, y_center, 1, 1);
					addPieceVertex(x_center, y_center, 1, 0);

					#undef addPieceVertex
				}
			}
		}

		for (uint32_t i = 0; i < 4; i++) {
			#define addVertex(U, V) \
				vertices[field_mesh.vertex_count++] = Vertex( \
					(float(i) - 1.5f) + (U - 0.5f) / 1.5f, (V - 0.5f) / 1.5f, \
					U * 1.5f - 0.25f, (1 - V) * 1.5f - 0.25f, \
					MAX_PLAYERS * 32 + i \
				)

			addVertex(1, 0);
			addVertex(0, 0);
			addVertex(0, 1);

			addVertex(0, 1);
			addVertex(1, 1);
			addVertex(1, 0);

			#undef addVertex
		}

		glBindBuffer(GL_ARRAY_BUFFER, field_mesh.handle);
		glBufferData(GL_ARRAY_BUFFER, field_mesh.vertex_count * sizeof(Vertex), vertices, GL_STATIC_DRAW);
		free(vertices);
	}

	void run() {
		while (!quit) {
			handleEvents();
			receiveMessageFromServer();

			glClear(GL_COLOR_BUFFER_BIT);

			render();

			ImGui_ImplOpenGL3_NewFrame();
			ImGui_ImplSDL3_NewFrame();
			ImGui::NewFrame();

			renderUI();

			ImGui::Render();
			ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

			SDL_GL_SwapWindow(window);
		}
	}

	void handleEvents() {
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			ImGui_ImplSDL3_ProcessEvent(&event);

			switch (event.type) {
				case SDL_EVENT_QUIT: quit = true; break;
				case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: onFramebufferResized(event.window); break;
				case SDL_EVENT_MOUSE_BUTTON_UP: if (!ImGui::GetIO().WantCaptureMouse) onMouseButtonUp(event.button); break;
				case SDL_EVENT_MOUSE_MOTION: if (!ImGui::GetIO().WantCaptureMouse) onMouseMoved(event.motion); break;
				case SDL_EVENT_MOUSE_WHEEL:  if (!ImGui::GetIO().WantCaptureMouse) onMouseWheel(event.wheel); break;
			}
		}
	}

	void render() {
		glBindBuffer(GL_UNIFORM_BUFFER, viewport_info_uniform_buffer);
		glBufferData(GL_UNIFORM_BUFFER, sizeof(ViewportInfoUniformData), &viewport_info_uniform_data, GL_DYNAMIC_DRAW);

		glBindBuffer(GL_UNIFORM_BUFFER, field_uniform_buffer);
		glBufferData(GL_UNIFORM_BUFFER, sizeof(field.tiles) + sizeof(uint32_t) * 5, &field.tiles, GL_DYNAMIC_DRAW);

		float time = float(SDL_GetTicks()) / 1000.0f;
		glBindBuffer(GL_UNIFORM_BUFFER, time_uniform_buffer);
		glBufferData(GL_UNIFORM_BUFFER, sizeof(float), &time, GL_DYNAMIC_DRAW);

		glUseProgram(field_shader);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, palette_texture);
		glUniform1i(0, 0);

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, spritesheet_texture);
		glUniform1i(1, 1);

		glBindBufferBase(GL_UNIFORM_BUFFER, 0, viewport_info_uniform_buffer);
		glBindBufferBase(GL_UNIFORM_BUFFER, 1, field_uniform_buffer);
		glBindBufferBase(GL_UNIFORM_BUFFER, 2, time_uniform_buffer);

		glBindVertexArray(field_mesh.vao);

		if (show_promotion_dialog) {
			glDrawArrays(GL_TRIANGLES, 0, field_mesh.vertex_count);
		} else {
			glDrawArrays(GL_TRIANGLES, 0, field_mesh.vertex_count - 4 * 6);
		}
	}

	void renderUI() {
		ImGui::Begin("test"); {
			ImGui::InputText("address", server_address.data(), server_address.size());
			ImGui::InputInt("port", &server_port);

			if (socket) {
				if (ImGui::Button("disconnect")) {
					disconnectFromServer();
				}
			} else {
				if (ImGui::Button("connect")) {
					connectToServer(server_address, server_port);
				}
			}

#ifndef NDEBUG
			ImGui::Separator();
			ImGui::Text("resolution: %f, %f", width, height);
			ImGui::SliderFloat("scale", &viewport_info_uniform_data.scale, 0.1f, 0.4f);
			ImGui::SliderFloat2("offset", &viewport_info_uniform_data.x_offset, -1.0f, 1.0f);
#endif

			if (!socket) {
				ImGui::Separator();

				if (ImGui::Button("reset field")) {
					field.initializeField();
				}

				if (ImGui::SliderInt("num players", (int*)&field.num_players, 2, MAX_PLAYERS)) {
					field.initializeNeighborGraph();
					field.initializeField();
					updateVertexBufferData();
				}
			}

#ifndef NDEBUG
			ImGui::Separator();

			if (ImGui::Button("surrender")) {
				field.players[field.current_player].is_checkmate = true;
				rotateToNextPlayer();
			}

			ImGui::Checkbox("mark attackers", &mark_attackers);
#endif
		} ImGui::End();
	}

	void receiveFieldFromServer() {
		if (!socket) {
			return;
		}

		int received = SDLNet_ReadFromStreamSocket(socket, &field, sizeof(Field));
		if (received < 0) {
			disconnectFromServer();
		} else if (received > 0) {
			while (received < (int)sizeof(Field)) {
				SDLNet_WaitUntilInputAvailable((void**)&socket, 1, -1);
				const int segment = SDLNet_ReadFromStreamSocket(socket, ((char*)&field) + received, sizeof(Field) - received);
				if (segment <= 0) {
					disconnectFromServer();
					break;
				}
				received += segment;
			}
		}
	}

	void receiveMessageFromServer() {
		if (!socket) {
			return;
		}

		Message msg;
		int received = SDLNet_ReadFromStreamSocket(socket, &msg, sizeof(Message));
		if (received == 0) {
			return;
		}

		if (received != sizeof(Message)) {
			disconnectFromServer();
		}

		switch (msg.type) {
			case Message::Move: {
				field.current_player = msg.next_player;
				if (msg.player == field.player_pov) {
					break;
				}
				field.moveFigure(msg.move.type, msg.move.from, msg.move.to);
			} break;
			case Message::Promotion: {
				field.current_player = msg.next_player;
				field.tiles[msg.promotion.id].figure = msg.promotion.figure;
			}
		}
	}

	void connectToServer(const std::string &address, uint16_t port) {
		SDLNet_Address *addr = SDLNet_ResolveHostname(address.c_str());
		SDLNet_WaitUntilResolved(addr, -1);
		socket = SDLNet_CreateClient(addr, port);

		if (SDLNet_WaitUntilConnected(socket, -1) != 1) {
			printf("couldn't connect to server (%s)\n", SDLNet_GetAddressString(addr));
			socket = NULL;
			return;
		}

		SDLNet_WaitUntilInputAvailable((void**)&socket, 1, -1);
		receiveFieldFromServer();

		if (field.num_players > MAX_PLAYERS) {
			panic("received invalid field from server\n");
		}

		printf("connected to server (%s), %i players\n", SDLNet_GetAddressString(addr), field.num_players);

		SDLNet_UnrefAddress(addr);

		updateVertexBufferData();
	}

	void disconnectFromServer() {
		printf("connection to server (%s) lost\n", SDLNet_GetAddressString(SDLNet_GetStreamSocketAddress(socket)));
		SDLNet_DestroyStreamSocket(socket);
		socket = NULL;
		abort();
	}

	void onFramebufferResized(SDL_WindowEvent event) {
		width = event.data1;
		height = event.data2;
		glViewport(0, 0, width, height);

		viewport_info_uniform_data.aspect_ratio = width / height;
	}

	void onMouseButtonUp(SDL_MouseButtonEvent event) {
		if (event.button == 1) {
			if (show_promotion_dialog) {
				promotePawn();
			} else {
				const MoveType move = field.tiles[field.cursor_id].move;
				for (uint32_t i = 0; i < field.num_players * 32; i++) {
					field.tiles[i].move = MoveType::None;
				}

				if (mark_attackers) {
					field.isTileAttacked(field.cursor_id, field.current_player, true);
				} else if (move != MoveType::None) {
					moveFigure(move, field.selected_id, field.cursor_id);
				} else if (field.tiles[field.cursor_id].player == field.player_pov && field.player_pov == field.current_player) {
					field.calculateMoves(field.cursor_id, true);
					field.selected_id = field.cursor_id;
				}
			}
		}
	}

	void onMouseMoved(SDL_MouseMotionEvent event) {
		if (event.state & SDL_BUTTON(3)) {
			viewport_info_uniform_data.x_offset = std::clamp(viewport_info_uniform_data.x_offset + event.xrel / width * 2, -1.0f, 1.0f);
			viewport_info_uniform_data.y_offset = std::clamp(viewport_info_uniform_data.y_offset - event.yrel / height * 2, -1.0f, 1.0f);
		} else {
			field.cursor_id = getTileUnderCursor((event.x / width * 2 - 1.0f) * (width / height), 1.0f - event.y / height * 2);
			if (field.cursor_id < MAX_PLAYERS * 32) {
				field.cursor_id = (field.cursor_id + (field.player_pov<<5)) % (field.num_players<<5);
			}
		}
	}

	void onMouseWheel(SDL_MouseWheelEvent event) {
		viewport_info_uniform_data.scale = std::clamp(viewport_info_uniform_data.scale + event.y * 0.025f, 0.1f, 0.4f);
	}

	void moveFigure(MoveType move, uint32_t from, uint32_t to) {
		field.moveFigure(move, from, to);

		if (socket) {
			Message msg = Message::makeMove(field.player_pov, from, to, move);
			if (SDLNet_WriteToStreamSocket(socket, &msg, sizeof(Message)) != 0) {
				SDLNet_DestroyStreamSocket(socket);
				socket = NULL;
			}
		}

		if (getY(to) == 0 && field.tiles[to].figure == Figure::Pawn) {
			field.selected_id = to;
			for (int64_t i = 0; i < 4; i++) {
				field.tiles[32 * MAX_PLAYERS + i].player = field.current_player;
			}
			show_promotion_dialog = true;
			return;
		}

		rotateToNextPlayer();
	}

	void promotePawn() {
		field.tiles[field.selected_id].figure = field.tiles[field.cursor_id].figure;
		show_promotion_dialog = false;

		rotateToNextPlayer();

		if (socket) {
			Message msg = Message::makePromotion(field.player_pov, field.selected_id, field.tiles[field.cursor_id].figure);
			if (SDLNet_WriteToStreamSocket(socket, &msg, sizeof(Message)) != 0) {
				SDLNet_DestroyStreamSocket(socket);
				socket = NULL;
			}
		}
	}

	void rotateToNextPlayer() {
		if (!isLocalGame()) {
			return;
		}

		do {
			field.current_player = (field.current_player + 1) % field.num_players;
			field.cursor_id = (field.cursor_id + 32) % (field.num_players * 32);

			if (field.isPlayerCheckMate(field.current_player)) {
				field.players[field.current_player].is_checkmate = true;
			}
		} while (field.players[field.current_player].is_checkmate);

		field.player_pov = field.current_player;
	}

	bool isLocalGame() {
		return socket == NULL;
	}

	uint32_t getTileUnderCursor(float x, float y) {
		if (show_promotion_dialog) {
			return MAX_PLAYERS * 32 + std::clamp(int(x / viewport_info_uniform_data.scale + 2), 0, 3);
		}

		x = (x - viewport_info_uniform_data.x_offset) / viewport_info_uniform_data.scale;
		y = (y - viewport_info_uniform_data.y_offset) / viewport_info_uniform_data.scale;

		const float angle = 1.0f - (atanf(-y / x) / (2.0f * PI) + 0.25f + 0.5f * (float)(x < 0.0f));
		const uint32_t half_segment = (field.num_players * 2 - (uint32_t)floorf(angle * field.num_players * 2.0f) + field.num_players) % (field.num_players * 2);

		const float angle_a = -((half_segment + 0.0f) / (field.num_players * 2.0f) * 2.0f * PI + PI / (1.0f + 1.0f / (field.num_players - 1.0f)));
		const float angle_b = -((half_segment + 1.0f) / (field.num_players * 2.0f) * 2.0f * PI + PI / (1.0f + 1.0f / (field.num_players - 1.0f)));

		const float x1 = -sinf(angle_a);
		const float y1 = +cosf(angle_a);

		const float x2 = -sinf(angle_b);
		const float y2 = +cosf(angle_b);

		// a * x1 + b * x2 = x
		// a * y1 + b * y2 = y
		const float b = (y * x1 - x * y1) / (y2 * x1 - x2 * y1);
		const float a = fabsf(x1) < 0.01 ? (y - b * y2) / y1 : (x - b * x2) / x1;

		uint32_t tile_x, tile_y;
		if (half_segment % 2) {
			tile_x = std::clamp(3 - std::floor(b), 0.0f, 7.0f);
			tile_y = 3 - std::clamp(std::floor(a), 0.0f, 3.0f);
		} else {
			tile_x = std::clamp(std::floor(a) + 4, 0.0f, 7.0f);
			tile_y = 3 - std::clamp(std::floor(b), 0.0f, 3.0f);
		}

		return getId(tile_x, tile_y, half_segment / 2);
	}

private:
	SDL_Window* window;
	SDL_GLContext gl_context;
	float width, height;

	bool quit = false;

	std::string server_address = "127.0.0.1" + std::string(256, '\0');
	int server_port = 1234;

	bool show_promotion_dialog = false;
	bool mark_attackers = false;

	SDLNet_StreamSocket *socket;

	GLuint field_shader;
	VertexBuffer field_mesh;
	GLuint palette_texture, spritesheet_texture;

	GLuint viewport_info_uniform_buffer;
	ViewportInfoUniformData viewport_info_uniform_data;

	GLuint field_uniform_buffer, time_uniform_buffer;

	Field field;
};

int main(int, char *[]) {
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS) != 0) {
		panic("Failed to initialize SDL\n");
	}

	if (SDLNet_Init() != 0) {
		panic("Failed to initialize SDL_net\n");
	}

	if (IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG | IMG_INIT_TIF | IMG_INIT_WEBP | IMG_INIT_JXL | IMG_INIT_AVIF) == 0) {
		panic("Failed to initialize SDL_image\n");
	}

	Game game(1000, 1000);
	game.run();

	IMG_Quit();
	SDLNet_Quit();
	SDL_Quit();

	return 0;
}
