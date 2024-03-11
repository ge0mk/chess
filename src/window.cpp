#include "window.hpp"

#include "chess.hpp"
#include "io.hpp"
#include "session.hpp"

#include "SDL3_image/SDL_image.h"

#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_opengl3.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#define PI 3.141592654f

namespace ImGui {
	template <typename ...Args>
	static inline void FText(std::format_string<Args...> fmt, Args &&...args) {
		const std::string text = std::format(fmt, std::forward<Args>(args)...);
		ImGui::TextUnformatted(text.data(), text.data() + text.size());
	}

	template <typename ...Args>
	static inline void FTextColored(ImVec4 col, std::format_string<Args...> fmt, Args &&...args) {
		const std::string text = std::format(fmt, std::forward<Args>(args)...);
		PushStyleColor(ImGuiCol_Text, col);
		ImGui::TextUnformatted(text.data(), text.data() + text.size());
		PopStyleColor();
	}
}

Window::Window(const std::vector<std::string> &) : viewport(1000, 1000) {
	// init sdl
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);

	SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

	window = SDL_CreateWindow("", viewport.width, viewport.height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);
	if (!window) {
		panic("Failed to create a window {}", SDL_GetError());
	}

	gl_context = SDL_GL_CreateContext(window);
	SDL_GL_MakeCurrent(window, gl_context);
	SDL_GL_SetSwapInterval(1);

	// init gl
	gladLoadGLLoader((void*(*)(const char*))SDL_GL_GetProcAddress);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glClearColor(0.2f, 0.2f, 0.2f, 0.0f);

	field_shader = loadProgram("field", {
#if defined(SPIRV_SHADERS)
		ShaderStage{"shaders/field.vert.spv", GL_VERTEX_SHADER, true, true},
		ShaderStage{"shaders/field.frag.spv", GL_FRAGMENT_SHADER, true, true}
#else
		ShaderStage{"shaders/field.vert", GL_VERTEX_SHADER, true, false},
		ShaderStage{"shaders/field.frag", GL_FRAGMENT_SHADER, true, false}
#endif
	}).value();

	field_mesh = Buffer<Vertex>::create();

	glGenVertexArrays(1, &field_mesh_vao);
	glBindVertexArray(field_mesh_vao);

	field_mesh.bind(GL_ARRAY_BUFFER);

	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), 0);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, u));
	glEnableVertexAttribArray(1);
	glVertexAttribIPointer(2, 1, GL_INT, sizeof(Vertex), (void*)offsetof(Vertex, id));
	glEnableVertexAttribArray(2);

	field_uniform_buffer = Buffer<Field>::create();
	viewport_uniform_buffer = Buffer<Viewport>::create();
	time_uniform_buffer = Buffer<float>::create();

	if (SDL_Surface *palette_surface = IMG_Load("textures/palette.png")) {
		palette = Texture::create(palette_surface);
		SDL_DestroySurface(palette_surface);
	}

	if (SDL_Surface *spritesheet_surface = IMG_Load("textures/spritesheet.png")) {
		spritesheet = Texture::create(spritesheet_surface);
		SDL_DestroySurface(spritesheet_surface);
	}

	// init imgui
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

	ImGui::StyleColorsDark();

	ImGui_ImplSDL3_InitForOpenGL(window, gl_context);
	ImGui_ImplOpenGL3_Init("#version 460");
}

Window::~Window() {
	palette.destroy();
	spritesheet.destroy();

	field_uniform_buffer.destroy();
	viewport_uniform_buffer.destroy();
	time_uniform_buffer.destroy();

	glDeleteVertexArrays(1, &field_mesh_vao);
	field_mesh.destroy();

	glDeleteProgram(field_shader);

	SDL_GL_DeleteContext(gl_context);
	SDL_DestroyWindow(window);
}

int Window::run() {
	SDL_ShowWindow(window);

	while (!quit) {
		handleEvents();

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

	return 0;
}

void Window::handleEvents() {
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		ImGui_ImplSDL3_ProcessEvent(&event);

		switch (event.type) {
			case SDL_EVENT_QUIT: quit = true; break;
			case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: onFramebufferResized(event.window); break;
			case SDL_EVENT_MOUSE_BUTTON_UP: onMouseButtonUp(event.button); break;
			case SDL_EVENT_MOUSE_MOTION: onMouseMoved(event.motion); break;
			case SDL_EVENT_MOUSE_WHEEL: onMouseWheel(event.wheel); break;
		}
	}

	if (mode == Mode::Client) {
		receiveMessageFromServer();
	} else if (mode & Mode::Host) {
		acceptQueuedPlayers();
		receiveMessagesFromClients();
	}
}

void Window::render() {
	viewport_uniform_buffer.bind(GL_UNIFORM_BUFFER);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(Viewport), &viewport, GL_DYNAMIC_DRAW);

	field_uniform_buffer.bind(GL_UNIFORM_BUFFER);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(field.tiles) + sizeof(uint32_t) * 5, &field.tiles, GL_DYNAMIC_DRAW);

	float time = float(SDL_GetTicks()) / 1000.0f;
	time_uniform_buffer.bind(GL_UNIFORM_BUFFER);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(float), &time, GL_DYNAMIC_DRAW);

	glUseProgram(field_shader);

	glActiveTexture(GL_TEXTURE0);
	palette.bind();
	glUniform1i(0, 0);

	glActiveTexture(GL_TEXTURE1);
	spritesheet.bind();
	glUniform1i(1, 1);

	glBindBufferBase(GL_UNIFORM_BUFFER, 0, viewport_uniform_buffer.handle);
	glBindBufferBase(GL_UNIFORM_BUFFER, 1, field_uniform_buffer.handle);
	glBindBufferBase(GL_UNIFORM_BUFFER, 2, time_uniform_buffer.handle);

	glBindVertexArray(field_mesh_vao);

	if (ui_state.show_promotion_dialog) {
		glDrawArrays(GL_TRIANGLES, 0, field_mesh_vertex_count);
	} else {
		glDrawArrays(GL_TRIANGLES, 0, field_mesh_vertex_count - 4 * 6);
	}
}

void Window::renderUI() {
	if (ImGui::Begin("main menu")) {
		ImGui::InputText("player name", &ui_state.player_name);

		ImGui::Separator();

		if (mode != Mode::None) {
			for (size_t i = 0; i < players.size(); i++) {
				ImVec4 color = i == field.current_player ? ImVec4(0.5, 1, 0.5, 1) : ImVec4(1, 1, 1, 1);
				if (i == field.player_pov && mode & Mode::Client) {
					ImGui::TextColored(color, "%lu: %s (you)", i, players[i].name.c_str());
				} else {
					ImGui::TextColored(color, "%lu: %s", i, players[i].name.c_str());
				}
			}

			ImGui::Separator();
		}

		if (mode == Mode::None) {
			static int num_players = 2;
			ImGui::SliderInt("players", &num_players, 2, 8);
			if (ImGui::Button("start local game")) {
				initLocal(num_players);
			}

			if (ImGui::Button("host LAN game")) {
				initHostHybrid(num_players, ui_state.server_port, ui_state.player_name);
			}

			if (ImGui::Button("host LAN game & spectate")) {
				initHost(num_players, ui_state.server_port);
			}

			ImGui::InputText("address", &ui_state.server_address);
			ImGui::InputInt("port", &ui_state.server_port);

			if (ImGui::Button("join LAN game")) {
				initClient(ui_state.server_address, ui_state.server_port, ui_state.player_name);
			}
		} else if (mode & Mode::Host) {
			if (ImGui::Button("cancel match")) {
				deinit();
			}
		} else if (mode == Mode::Client) {
			if (ImGui::Button("disconnect")) {
				disconnectFromServer();
			}
		}

		ImGui::Separator();

		for (const Event &event : log) {
			switch (event.kind) {
				case Session::Event::Move: {
					ImGui::FTextColored(ImVec4(1, 1, 1, 1), "{}: Move ({}, {}, {}) -> ({}, {}, {})", event.player,
						getX(event.from), getY(event.from), getZ(event.from),
						getX(event.to), getY(event.to), getZ(event.to)
					);
				} break;
				case Session::Event::Capture: {
					ImGui::FTextColored(ImVec4(1, 0.8, 0.5, 1), "{}: Capture ({}, {}, {}) -> ({}, {}, {})", event.player,
						getX(event.from), getY(event.from), getZ(event.from),
						getX(event.to), getY(event.to), getZ(event.to)
					);
				} break;
				case Session::Event::Castle: {
					ImGui::FTextColored(ImVec4(0.5, 1, 0.5, 1), "{}: Castle ({}, {}, {}) -> ({}, {}, {})", event.player,
						getX(event.from), getY(event.from), getZ(event.from),
						getX(event.to), getY(event.to), getZ(event.to)
					);
				} break;
				case Session::Event::EnPassant: {
					ImGui::FTextColored(ImVec4(1, 0.8, 0.5, 1), "{}: Capture en passant ({}, {}, {}) -> ({}, {}, {})", event.player,
						getX(event.from), getY(event.from), getZ(event.from),
						getX(event.to), getY(event.to), getZ(event.to)
					);
				} break;
				case Session::Event::Promote: {
					ImGui::FTextColored(ImVec4(0.5, 0.5, 1, 1), "{}: Promote ({}, {}, {}) to {}", event.player,
						getX(event.from), getY(event.from), getZ(event.from),
						event.promotion
					);
				} break;
				case Session::Event::Check: {
				} break;
				case Session::Event::CheckMate: {
				} break;
				case Session::Event::Surrender: {
				} break;
			}
		}
	} ImGui::End();
}

void Window::updateVertexBuffer() {
	field_mesh_vertex_count = (field.num_players * 32 * 2) * 6 + 4 * 6;
	Vertex *vertices = (Vertex*)malloc(field_mesh_vertex_count * sizeof(Vertex));

	if (!vertices) {
		panic("out of memory");
	}

	uint64_t i = 0;
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
					vertices[i++] = Vertex( \
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
					vertices[i++] = Vertex( \
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

	for (uint32_t k = 0; k < 4; k++) {
		#define addVertex(U, V) \
			vertices[i++] = Vertex( \
				(float(k) - 1.5f) + (U - 0.5f) / 1.5f, (V - 0.5f) / 1.5f, \
				U * 1.5f - 0.25f, (1 - V) * 1.5f - 0.25f, \
				MAX_PLAYERS * 32 + k \
			)

		addVertex(1, 0);
		addVertex(0, 0);
		addVertex(0, 1);

		addVertex(0, 1);
		addVertex(1, 1);
		addVertex(1, 0);

		#undef addVertex
	}

	field_mesh.data(GL_ARRAY_BUFFER, std::span<Vertex>(vertices, field_mesh_vertex_count), GL_STATIC_DRAW);
	free(vertices);
}

uint32_t Window::getTileUnderCursor(float x, float y) {
	if (field.num_players == 0) {
		return 0;
	}

	if (ui_state.show_promotion_dialog) {
		return MAX_PLAYERS * 32 + std::clamp(int(x / viewport.scale + 2), 0, 3);
	}

	x = (x - viewport.x_offset) / viewport.scale;
	y = (y - viewport.y_offset) / viewport.scale;

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

void Window::onFramebufferResized(const SDL_WindowEvent &event) {
	viewport.width = event.data1;
	viewport.height = event.data2;
	viewport.aspect_ratio = viewport.width / viewport.height;
	glViewport(0, 0, viewport.width, viewport.height);
}

void Window::onMouseButtonUp(const SDL_MouseButtonEvent &event) {
	if (ImGui::GetIO().WantCaptureMouse) {
		return;
	}

	if (event.button == 1) {
		if (ui_state.show_promotion_dialog) {
			promoteFigure(field.selected_id, field.tiles[field.cursor_id].figure);
		} else {
			const MoveType type = field.tiles[field.cursor_id].move;
			for (uint32_t i = 0; i < field.num_players * 32; i++) {
				field.tiles[i].move = MoveType::None;
			}

			if (type != MoveType::None) {
				moveFigure(field.selected_id, field.cursor_id, type);
			} else if (field.tiles[field.cursor_id].player == field.player_pov && field.player_pov == field.current_player) {
				field.calculateMoves(field.cursor_id, true);
				field.selected_id = field.cursor_id;
			}
		}
	}
}

void Window::onMouseMoved(const SDL_MouseMotionEvent &event) {
	if (ImGui::GetIO().WantCaptureMouse) {
		return;
	}

	if (event.state & SDL_BUTTON(3)) {
		viewport.x_offset = std::clamp(viewport.x_offset + event.xrel / viewport.width * 2, -1.0f, 1.0f);
		viewport.y_offset = std::clamp(viewport.y_offset - event.yrel / viewport.height * 2, -1.0f, 1.0f);
	} else {
		if (field.num_players > 0) {
			field.cursor_id = getTileUnderCursor((event.x / viewport.width * 2 - 1.0f) * viewport.aspect_ratio, 1.0f - event.y / viewport.height * 2);
			if (field.cursor_id < MAX_PLAYERS * 32) {
				field.cursor_id = (field.cursor_id + (field.player_pov<<5)) % (field.num_players<<5);
			}
		}
	}
}

void Window::onMouseWheel(const SDL_MouseWheelEvent &event) {
	if (ImGui::GetIO().WantCaptureMouse) {
		return;
	}

	viewport.scale = std::clamp(viewport.scale + event.y * 0.025f, 0.1f, 0.4f);
}

void Window::onFieldInitialized() {
	updateVertexBuffer();
}

void Window::onFigureMoved(uint32_t player, uint32_t from, uint32_t to, MoveType type) {
	if (getY(to) == 0 && field.tiles[to].figure == Figure::Pawn) {
		ui_state.show_promotion_dialog = true;
		field.selected_id = to;
	}

	Session::onFigureMoved(player, from, to, type);
}

void Window::onFigurePromoted(uint32_t player, uint32_t id, Figure to) {
	ui_state.show_promotion_dialog = false;
	Session::onFigurePromoted(player, id, to);
}
