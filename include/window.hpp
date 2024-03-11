#pragma once

#include "session.hpp"
#include "gl.hpp"

#include <string>
#include <vector>

struct Vertex {
	float x, y;
	float u, v;
	uint32_t id;

	inline constexpr Vertex(float x, float y, float u, float v, uint32_t id) : x(x), y(y), u(u), v(v), id(id) {}
};

class Window : public Session {
public:
	Window(const std::vector<std::string> &args);
	~Window();

	static inline int run(const std::vector<std::string> &args) {
		return Window(args).run();
	}

	int run();

	void handleEvents();
	void render();
	void renderUI();

	void updateVertexBuffer();

	uint32_t getTileUnderCursor(float x, float y);

	void onFramebufferResized(const SDL_WindowEvent &event);
	void onMouseButtonUp(const SDL_MouseButtonEvent &event);
	void onMouseMoved(const SDL_MouseMotionEvent &event);
	void onMouseWheel(const SDL_MouseWheelEvent &event);

	void onFieldInitialized() override;
	void onFigureMoved(uint32_t player, uint32_t from, uint32_t to, MoveType type) override;
	void onFigurePromoted(uint32_t player, uint32_t id, Figure to) override;

private:
	SDL_Window *window;
	SDL_GLContext gl_context;

	bool quit = false;

	struct Viewport {
		float width, height;
		float x_offset, y_offset;
		float scale;
		float aspect_ratio;

		inline Viewport(float width, float height)
			: width(width), height(height),
			  x_offset(0.0f), y_offset(0.0f),
			  scale(0.2f), aspect_ratio(width / height) {}
	} viewport;

	struct UIState {
		bool show_main_menu = true;
		bool show_promotion_dialog = false;
		std::string player_name = "player#" + std::to_string(time(nullptr) % 100);
		std::string server_address = "127.0.0.1";
		int server_port = 1234;
	} ui_state;

	GLuint field_shader;

	Buffer<Vertex> field_mesh;
	uint64_t field_mesh_vertex_count = 0;
	GLuint field_mesh_vao;

	Buffer<Field> field_uniform_buffer;
	Buffer<Viewport> viewport_uniform_buffer;
	Buffer<float> time_uniform_buffer;

	Texture palette, spritesheet;
};
