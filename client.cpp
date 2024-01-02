#include "SDL_surface.h"
#include "chess.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <iterator>
#include <fstream>
#include <span>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3_net/SDL_net.h>
#include <SDL3_image/SDL_image.h>

#include "glad.h"

#define PI 3.141592654f

#define panic(format, ...) ({ \
	fprintf(stderr, format, ##__VA_ARGS__); \
	abort(); \
})

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
};

struct VertexBuffer {
	GLuint handle;
	GLuint vao;
	uint64_t vertex_count;
};

struct ViewportInfoUniformData {
	float aspect_ratio;
};

struct FieldShaderUniformData {
	uint32_t num_players;
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
	GLuint shaders[num_shaders];

	va_list ptr;
	va_start(ptr, num_shaders);
	for (uint64_t i = 0; i < num_shaders; i++) {
		const GLenum type = va_arg(ptr, GLenum);
		const ShaderFormat format = va_arg(ptr, ShaderFormat);
		const ShaderSource source = va_arg(ptr, ShaderSource);
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
	Game(float width, float height, const std::string& server_addr, uint16_t server_port) : width(width), height(height) {
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

		gladLoadGL();

		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);

		loadShaders();
		loadTextures();

		connectToServer(server_addr, server_port);
		scale = 0.5f / field.num_players;

		generateMeshes();
		initUniformBuffers();
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
			GL_VERTEX_SHADER, ShaderFormat_SPIRV, ShaderSource_File, "build/shaders/field.vert.spv",
			GL_FRAGMENT_SHADER, ShaderFormat_SPIRV, ShaderSource_File, "build/shaders/field.frag.spv"
	#else
			GL_VERTEX_SHADER, ShaderFormat_GLSL, ShaderSource_File, "shaders/field.vert",
			GL_FRAGMENT_SHADER, ShaderFormat_GLSL, ShaderSource_File, "shaders/field.frag"
	#endif
		);

		piece_shader = loadProgram(2,
	#if defined(SPIRV_SHADERS)
			GL_VERTEX_SHADER, ShaderFormat_SPIRV, ShaderSource_File, "build/shaders/piece.vert.spv",
			GL_FRAGMENT_SHADER, ShaderFormat_SPIRV, ShaderSource_File, "build/shaders/piece.frag.spv"
	#else
			GL_VERTEX_SHADER, ShaderFormat_GLSL, ShaderSource_File, "shaders/piece.vert",
			GL_FRAGMENT_SHADER, ShaderFormat_GLSL, ShaderSource_File, "shaders/piece.frag"
	#endif
		);
	}

	void initUniformBuffers() {
		viewport_info_uniform_data.aspect_ratio = width / height;
		field_shader_uniform_data.num_players = field.num_players;

		glGenBuffers(1, &viewport_info_uniform_buffer);
		glBindBuffer(GL_UNIFORM_BUFFER, viewport_info_uniform_buffer);
		glBufferData(GL_UNIFORM_BUFFER, sizeof(ViewportInfoUniformData), &viewport_info_uniform_data, GL_DYNAMIC_DRAW);

		glGenBuffers(1, &field_shader_uniform_buffer);
		glBindBuffer(GL_UNIFORM_BUFFER, field_shader_uniform_buffer);
		glBufferData(GL_UNIFORM_BUFFER, sizeof(FieldShaderUniformData), &field_shader_uniform_data, GL_DYNAMIC_DRAW);

		glGenBuffers(1, &piece_shader_uniform_buffer);
		glBindBuffer(GL_UNIFORM_BUFFER, piece_shader_uniform_buffer);
		glBufferData(GL_UNIFORM_BUFFER, sizeof(field.tiles) + sizeof(field.cursor_id), &field.tiles, GL_DYNAMIC_DRAW);
	}

	void loadTextures() {
		glGenTextures(1, &spritesheet_texture);
		glBindTexture(GL_TEXTURE_2D, spritesheet_texture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		SDL_Surface *spritesheet = IMG_Load("spritesheet.png");
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, spritesheet->w, spritesheet->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, spritesheet->pixels);
		SDL_DestroySurface(spritesheet);

		glGenerateMipmap(GL_TEXTURE_2D);
	}

	void generateMeshes() {
		field_mesh.vertex_count = 0;
		piece_mesh.vertex_count = 0;

		const uint64_t vertex_count = (field.num_players * 32) * 6;
		Vertex *field_vertices = (Vertex*)malloc(vertex_count * sizeof(Vertex));
		Vertex *piece_vertices = (Vertex*)malloc(vertex_count * sizeof(Vertex));

		if (!field_vertices || !piece_vertices) {
			panic("out of memory\n");
		}

		for (uint64_t segment = 0; segment < field.num_players * 2; segment++) {
			const float angle_a = (float)(segment + 0) / (float)(field.num_players * 2) * 2.0f * PI;
			const float angle_b = (float)(segment + 1) / (float)(field.num_players * 2) * 2.0f * PI;

			const float delta_a_x = -sinf(angle_a) * scale;
			const float delta_a_y = +cosf(angle_a) * scale;

			const float delta_b_x = -sinf(angle_b) * scale;
			const float delta_b_y = +cosf(angle_b) * scale;

			for (uint32_t a = 0; a < 4; a++) {
				for (uint32_t b = 0; b < 4; b++) {
					const uint32_t field_x = (segment % 2) ? (3 - b) : a + 4;
					const uint32_t field_y = 3 - ((segment % 2) ? a : b);
					const uint32_t field_z = segment >> 1;

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
						field_vertices[field_mesh.vertex_count++] = (Vertex){ \
							.x = X, .y = Y, \
							.u = U, .v = V, \
							.id = getId(field_x, field_y, field_z), \
						};

					addFieldVertex(x1, y1, 1, 0);
					addFieldVertex(x0, y0, 0, 0);
					addFieldVertex(x2, y2, 0, 1);

					addFieldVertex(x2, y2, 0, 1);
					addFieldVertex(x3, y3, 1, 1);
					addFieldVertex(x1, y1, 1, 0);

					#undef addFieldVertex

					#define addPieceVertex(X, Y, U, V) \
						piece_vertices[piece_mesh.vertex_count++] = (Vertex){ \
							.x = X + (U - 0.5f) * scale / 2, .y = Y + (V - 0.5f) * scale / 2, \
							.u = U, .v = 1 - V, \
							.id = getId(field_x, field_y, field_z), \
						};

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

		glGenVertexArrays(1, &field_mesh.vao);
		glBindVertexArray(field_mesh.vao);

		glGenBuffers(1, &field_mesh.handle);
		glBindBuffer(GL_ARRAY_BUFFER, field_mesh.handle);
		glBufferData(GL_ARRAY_BUFFER, field_mesh.vertex_count * sizeof(Vertex), field_vertices, GL_STATIC_DRAW);
		free(field_vertices);

		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), 0);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, u));
		glEnableVertexAttribArray(1);
		glVertexAttribIPointer(2, 1, GL_INT, sizeof(Vertex), (void*)offsetof(Vertex, id));
		glEnableVertexAttribArray(2);

		glGenVertexArrays(1, &piece_mesh.vao);
		glBindVertexArray(piece_mesh.vao);

		glGenBuffers(1, &piece_mesh.handle);
		glBindBuffer(GL_ARRAY_BUFFER, piece_mesh.handle);
		glBufferData(GL_ARRAY_BUFFER, piece_mesh.vertex_count * sizeof(Vertex), piece_vertices, GL_STATIC_DRAW);
		free(piece_vertices);

		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), 0);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, u));
		glEnableVertexAttribArray(1);
		glVertexAttribIPointer(2, 1, GL_INT, sizeof(Vertex), (void*)offsetof(Vertex, id));
		glEnableVertexAttribArray(2);
	}

	void run() {
		while (!quit) {
			handleEvents();
			receiveFromServer();

			float xpos, ypos;
			SDL_GetMouseState(&xpos, &ypos);
			field.cursor_id = getTileUnderCursor((xpos / width * 2 - 1.0f) * (width / height), 1.0f - ypos / height * 2, field.num_players, scale);

			render();
		}
	}

	void handleEvents() {
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			switch (event.type) {
				case SDL_EVENT_QUIT: quit = true; break;
				case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: onFramebufferResized(event.window); break;
				case SDL_EVENT_MOUSE_BUTTON_UP: onMouseButtonUp(event.button);
			}
		}
	}

	void render() {
		glClear(GL_COLOR_BUFFER_BIT);

		glBindBuffer(GL_UNIFORM_BUFFER, piece_shader_uniform_buffer);
		glBufferData(GL_UNIFORM_BUFFER, sizeof(field.tiles) + sizeof(field.cursor_id), &field.tiles, GL_DYNAMIC_DRAW);

		glUseProgram(field_shader);
		glBindBufferBase(GL_UNIFORM_BUFFER, 0, viewport_info_uniform_buffer);
		glBindBufferBase(GL_UNIFORM_BUFFER, 1, field_shader_uniform_buffer);
		glBindVertexArray(field_mesh.vao);
		glDrawArrays(GL_TRIANGLES, 0, field_mesh.vertex_count);

		glUseProgram(piece_shader);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, spritesheet_texture);

		glBindBufferBase(GL_UNIFORM_BUFFER, 0, viewport_info_uniform_buffer);
		glBindBufferBase(GL_UNIFORM_BUFFER, 1, piece_shader_uniform_buffer);
		glUniform1i(0, 0);

		glBindVertexArray(piece_mesh.vao);
		glDrawArrays(GL_TRIANGLES, 0, piece_mesh.vertex_count);

		SDL_GL_SwapWindow(window);
	}

	void receiveFromServer() {
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

	void connectToServer(const std::string &address, uint16_t port) {
		SDLNet_Address *addr = SDLNet_ResolveHostname(address.c_str());
		SDLNet_WaitUntilResolved(addr, -1);
		socket = SDLNet_CreateClient(addr, port);

		if (!socket) {
			panic("Failed to connect to server\n");
		}

		SDLNet_WaitUntilConnected(socket, -1);
		SDLNet_WaitUntilInputAvailable((void**)&socket, 1, -1);
		receiveFromServer();

		if (field.num_players > MAX_PLAYERS) {
			panic("received invalid field from server\n");
		}

		printf("connected to server (%s), %i players\n", SDLNet_GetAddressString(addr), field.num_players);

		SDLNet_UnrefAddress(addr);
	}

	void disconnectFromServer() {
		printf("connection to server (%s) lost\n", SDLNet_GetAddressString(SDLNet_GetStreamSocketAddress(socket)));
		SDLNet_DestroyStreamSocket(socket);
		socket = NULL;
	}

	void onFramebufferResized(SDL_WindowEvent event) {
		width = event.data1;
		height = event.data2;
		glViewport(0, 0, width, height);

		viewport_info_uniform_data.aspect_ratio = width / height;
		glBindBuffer(GL_UNIFORM_BUFFER, viewport_info_uniform_buffer);
		glBufferData(GL_UNIFORM_BUFFER, sizeof(ViewportInfoUniformData), &viewport_info_uniform_data, GL_DYNAMIC_DRAW);
	}

	void onMouseButtonUp(SDL_MouseButtonEvent event) {
		if (event.button == 1) {
			if (field.tiles[field.cursor_id].is_reachable) {
				for (uint64_t i = 0; i < field.num_players * 32; i++) {
					field.tiles[i].is_reachable = 0;
				}
				moveFigure(field.selected_id, field.cursor_id);
			} else {
				for (uint64_t i = 0; i < field.num_players * 32; i++) {
					field.tiles[i].is_reachable = 0;
				}
				markReachableNodes(&field, field.cursor_id, field.tiles[field.cursor_id].figure);
				field.selected_id = field.cursor_id;
			}
		}
	}

	void moveFigure(uint32_t from, uint32_t to) {
		field.tiles[to].figure = field.tiles[from].figure;
		field.tiles[to].player = field.tiles[from].player;
		field.tiles[to].was_moved = true;
		field.tiles[from].figure = None;

		if (socket) {
			if (SDLNet_WriteToStreamSocket(socket, &field, sizeof(Field)) != 0) {
				SDLNet_DestroyStreamSocket(socket);
				socket = NULL;
			}
		}
	}

	uint64_t getTileUnderCursor(float x, float y, float segment_count, float scale) {
		const float angle = 1.0f - (atanf(-y / x) / (2.0f * PI) + 0.25f + 0.5f * (float)(x < 0.0f));
		const uint64_t tile_z = floorf(angle * segment_count);

		const float half_segment = floorf(angle * segment_count * 2);

		const float angle_a = (half_segment + 0) / (segment_count * 2) * 2.0f * PI;
		const float angle_b = (half_segment + 1) / (segment_count * 2) * 2.0f * PI;

		const float x1 = -sinf(angle_a) * scale;
		const float y1 = +cosf(angle_a) * scale;

		const float x2 = -sinf(angle_b) * scale;
		const float y2 = +cosf(angle_b) * scale;

		// a * x1 + b * x2 = x
		// a * y1 + b * y2 = y
		const float b = (y * x1 - x * y1) / (y2 * x1 - x2 * y1);
		const float a = fabsf(x1) < 0.01 ? (y - b * y2) / y1 : (x - b * x2) / x1;

		uint64_t tile_x, tile_y;
		if ((int)half_segment % 2) {
			tile_x = std::clamp(3 - std::floor(b), 0.0f, 7.0f);
			tile_y = 3 - std::clamp(std::floor(a), 0.0f, 3.0f);
		} else {
			tile_x = std::clamp(std::floor(a) + 4, 0.0f, 7.0f);
			tile_y = 3 - std::clamp(std::floor(b), 0.0f, 3.0f);
		}

		return getId(tile_x, tile_y, tile_z);
	}

private:
	SDL_Window* window;
	SDL_GLContext gl_context;
	float width, height;

	bool quit;
	float scale;

	SDLNet_StreamSocket *socket;

	GLuint field_shader, piece_shader;
	VertexBuffer field_mesh, piece_mesh;
	GLuint spritesheet_texture;

	GLuint viewport_info_uniform_buffer;
	ViewportInfoUniformData viewport_info_uniform_data;

	GLuint field_shader_uniform_buffer;
	FieldShaderUniformData field_shader_uniform_data;

	GLuint piece_shader_uniform_buffer;

	Field field;
};

int main([[maybe_unused]] int argc, char *argv[]) {
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS) != 0) {
		panic("Failed to initialize SDL_net\n");
	}

	if (SDLNet_Init() != 0) {
		panic("Failed to initialize SDL_net\n");
	}

	if (IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG | IMG_INIT_TIF | IMG_INIT_WEBP | IMG_INIT_JXL | IMG_INIT_AVIF) == 0) {
		panic("Failed to initialize SDL_image\n");
	}

	Game game(1000, 1000, argv[1], atoi(argv[2]));
	game.run();

	IMG_Quit();
	SDLNet_Quit();
	SDL_Quit();

	return 0;
}
