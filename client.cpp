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
#include <SDL3/SDL_surface.h>
#include <SDL3_net/SDL_net.h>
#include <SDL3_image/SDL_image.h>

#include "glad.h"

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_sdl3.h"
#include "imgui/backends/imgui_impl_opengl3.h"

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
	float x_offset;
	float y_offset;
	float scale;
	float aspect_ratio;
};

struct FieldShaderUniformData {
	uint32_t num_players;
	uint32_t player;
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
	Game(float width, float height) : width(width), height(height) {
		field.num_players = 3;
		field.player = 0;
		initializeNeighborGraph(&field);
		initializeField(&field);

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
		glClearColor(0.2f, 0.2f, 0.2f, 0.0f);

		viewport_info_uniform_data.scale = 0.2f;
		viewport_info_uniform_data.x_offset = 0.0f;
		viewport_info_uniform_data.y_offset = 0.0f;

		field_shader_uniform_data.num_players = field.num_players;

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

	void createUniformBuffers() {
		viewport_info_uniform_data.aspect_ratio = width / height;

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


		glGenVertexArrays(1, &piece_mesh.vao);
		glBindVertexArray(piece_mesh.vao);

		glGenBuffers(1, &piece_mesh.handle);
		glBindBuffer(GL_ARRAY_BUFFER, piece_mesh.handle);

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
		piece_mesh.vertex_count = 0;

		const uint64_t vertex_count = (field.num_players * 32) * 6;
		Vertex *field_vertices = (Vertex*)malloc(vertex_count * sizeof(Vertex));
		Vertex *piece_vertices = (Vertex*)malloc(vertex_count * sizeof(Vertex));

		if (!field_vertices || !piece_vertices) {
			panic("out of memory\n");
		}

		for (uint64_t half_segment = 0; half_segment < field.num_players * 2; half_segment++) {
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
							.x = X + (U - 0.5f) / 2, .y = Y + (V - 0.5f) / 2, \
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

		glBindBuffer(GL_ARRAY_BUFFER, field_mesh.handle);
		glBufferData(GL_ARRAY_BUFFER, field_mesh.vertex_count * sizeof(Vertex), field_vertices, GL_STATIC_DRAW);
		free(field_vertices);

		glBindBuffer(GL_ARRAY_BUFFER, piece_mesh.handle);
		glBufferData(GL_ARRAY_BUFFER, piece_mesh.vertex_count * sizeof(Vertex), piece_vertices, GL_STATIC_DRAW);
		free(piece_vertices);
	}

	void run() {
		while (!quit) {
			handleEvents();
			receiveFromServer();

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

		field_shader_uniform_data.num_players = field.num_players;
		glBindBuffer(GL_UNIFORM_BUFFER, field_shader_uniform_buffer);
		glBufferData(GL_UNIFORM_BUFFER, sizeof(FieldShaderUniformData), &field_shader_uniform_data, GL_DYNAMIC_DRAW);

		glBindBuffer(GL_UNIFORM_BUFFER, piece_shader_uniform_buffer);
		glBufferData(GL_UNIFORM_BUFFER, sizeof(field.tiles) + sizeof(uint32_t) * 4, &field.tiles, GL_DYNAMIC_DRAW);

		// draw field
		glUseProgram(field_shader);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, palette_texture);
		glUniform1i(0, 0);

		glBindBufferBase(GL_UNIFORM_BUFFER, 0, viewport_info_uniform_buffer);
		glBindBufferBase(GL_UNIFORM_BUFFER, 1, field_shader_uniform_buffer);
		glBindVertexArray(field_mesh.vao);
		glDrawArrays(GL_TRIANGLES, 0, field_mesh.vertex_count);

		// draw pieces
		glUseProgram(piece_shader);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, palette_texture);
		glUniform1i(0, 0);

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, spritesheet_texture);
		glUniform1i(1, 1);

		glBindBufferBase(GL_UNIFORM_BUFFER, 0, viewport_info_uniform_buffer);
		glBindBufferBase(GL_UNIFORM_BUFFER, 1, piece_shader_uniform_buffer);

		glBindVertexArray(piece_mesh.vao);
		glDrawArrays(GL_TRIANGLES, 0, piece_mesh.vertex_count);
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

			ImGui::Separator();

			ImGui::Text("resolution: %f, %f", width, height);
			ImGui::SliderFloat("scale", &viewport_info_uniform_data.scale, 0.1f, 0.4f);
			ImGui::SliderFloat2("offset", &viewport_info_uniform_data.x_offset, -1.0f, 1.0f);

			ImGui::Separator();

			if (ImGui::Button("reset field")) {
				initializeField(&field);
			}

			if (ImGui::SliderInt("num players", (int*)&field.num_players, 2, MAX_PLAYERS)) {
				field_shader_uniform_data.num_players = field.num_players;
				initializeNeighborGraph(&field);
				initializeField(&field);
				updateVertexBufferData();
			}

			if (ImGui::SliderInt("current player", (int*)&field.player, 0, field.num_players - 1)) {
				field_shader_uniform_data.player = field.player;
			}
		} ImGui::End();
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

		updateVertexBufferData();
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

	void onMouseMoved(SDL_MouseMotionEvent event) {
		if (event.state & SDL_BUTTON(3)) {
			viewport_info_uniform_data.x_offset = std::clamp(viewport_info_uniform_data.x_offset + event.xrel / width * 2, -1.0f, 1.0f);
			viewport_info_uniform_data.y_offset = std::clamp(viewport_info_uniform_data.y_offset - event.yrel / height * 2, -1.0f, 1.0f);
		} else {
			field.cursor_id = getTileUnderCursor((event.x / width * 2 - 1.0f) * (width / height), 1.0f - event.y / height * 2);
			field.cursor_id = (field.cursor_id + (field.player<<5)) % (field.num_players<<5);
		}
	}

	void onMouseWheel(SDL_MouseWheelEvent event) {
		viewport_info_uniform_data.scale = std::clamp(viewport_info_uniform_data.scale + event.y * 0.025f, 0.1f, 0.4f);
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

	uint64_t getTileUnderCursor(float x, float y) {
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

		uint64_t tile_x, tile_y;
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

	bool quit;

	std::string server_address = "127.0.0.1" + std::string(256, '\0');
	int server_port = 1234;

	SDLNet_StreamSocket *socket;

	GLuint field_shader, piece_shader;
	VertexBuffer field_mesh, piece_mesh;
	GLuint palette_texture, spritesheet_texture;

	GLuint viewport_info_uniform_buffer;
	ViewportInfoUniformData viewport_info_uniform_data;

	GLuint field_shader_uniform_buffer;
	FieldShaderUniformData field_shader_uniform_data;

	GLuint piece_shader_uniform_buffer;

	Field field;
};

int main(int, char *[]) {
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS) != 0) {
		panic("Failed to initialize SDL_net\n");
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
