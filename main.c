#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>
#include <string.h>

#include "glad.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define PI 3.141592654f

#define getId(x, y, z) (((z)<<5) | ((y)<<3) | (x))
#define getX(id) ((id) & 0b00000111)
#define getY(id) (((id)>>3) & 0b00000011)
#define getXY(id) ((id) & 0b00011111)
#define getZ(id) ((id)>>5)
#define packIdAndDirection(id, direction) (((id)<<2) | (direction))
#define extractId(v) ((v)>>2)
#define extractDirection(v) ((v) & 0b00000011)

#define min(a, b) ({ \
	const typeof(a) _a = a; \
	const typeof(b) _b = b; \
	_a < _b ? _a : _b; \
})

#define max(a, b) ({ \
	const typeof(a) _a = a; \
	const typeof(b) _b = b; \
	_a > _b ? _a : _b; \
})

#define clamp(a, min_val, max_val) (max(min(a, max_val), min_val))

#define panic(format, ...) ({ \
	fprintf(stderr, format, ##__VA_ARGS__); \
	exit(EXIT_FAILURE); \
})

typedef enum ShaderSource {
	ShaderSource_File,
	ShaderSource_Memory,
} ShaderSource;

typedef enum ShaderFormat {
	ShaderFormat_GLSL,
	ShaderFormat_SPIRV,
} ShaderFormat;

typedef struct {
	uint8_t *data;
	uint64_t size;
} Buffer;

typedef struct {
	float x, y;
	float u, v;
	uint32_t id;
} Vertex;

typedef struct {
	GLuint handle;
	GLuint vao;
	uint64_t vertex_count;
} VertexBuffer;

typedef struct {
	float aspect_ratio;
} ViewportInfoUniformData;

typedef struct {
	uint32_t num_players;
} FieldShaderUniformData;

#define MAX_PLAYERS 10

enum Direction {
	North,
	East,
	South,
	West,
};

typedef enum FigureType {
	None,
	Pawn,
	Bishop,
	Knight,
	Rook,
	Queen,
	King,
} FigureType;

typedef struct {
	uint32_t neighbors[4];
	uint32_t id;
	uint32_t figure;
	uint32_t player;
	uint32_t is_reachable;
} Tile;

typedef struct {
	Tile tiles[32 * MAX_PLAYERS];
	uint32_t num_players;
	uint32_t cursor_id;
	uint32_t selected_id;
} Field;

Buffer loadFile(const char *path) {
	FILE *file = fopen(path, "r");
	if (!file) {
		panic("Failed to load file %s: %s\n", path, strerror(errno));
	}

	static const uint64_t chunk_size = 4095;
	uint8_t *data = malloc(chunk_size + 1);
	uint64_t read = fread(data, 1, chunk_size, file);
	uint64_t size = read;

	while (read == chunk_size) {
		data = realloc(data, size + chunk_size + 1);
		read = fread(data + size, 1, chunk_size, file);
		size += read;
	}

	fclose(file);
	data[size] = 0;
	return (Buffer){data, size};
}

GLuint loadShader(Buffer source, GLenum type, ShaderFormat format) {
	if (source.size == 0) {
		return -1;
	}

	GLuint shader = glCreateShader(type);

	switch (format) {
		case ShaderFormat_GLSL: {
			glShaderSource(shader, 1, (const char**)&source.data, NULL);
			glCompileShader(shader);
		} break;
		case ShaderFormat_SPIRV: {
			glShaderBinary(1, &shader, GL_SHADER_BINARY_FORMAT_SPIR_V_ARB, source.data, source.size);
			glSpecializeShader(shader, "main", 0, NULL, NULL);
		} break;
	}

	int success;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

	if(!success) {
		char info_log[2048];
		glGetShaderInfoLog(shader, 2048, NULL, info_log);
		panic("Failed to compile shader: \n%s", info_log);
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
			const Buffer content = loadFile(path_or_data);
			shaders[i] = loadShader(content, type, format);
			free(content.data);
		} else if (source == ShaderSource_Memory) {
			const uint64_t size = va_arg(ptr, uint64_t);
			shaders[i] = loadShader((Buffer){
				.data = (uint8_t*)path_or_data,
				.size = size
			}, type, format);
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

void generateMeshes(uint64_t n, float scale, VertexBuffer *field_mesh, VertexBuffer *piece_mesh) {
	field_mesh->vertex_count = 0;
	piece_mesh->vertex_count = 0;

	const uint64_t vertex_count = (n * 32) * 6;
	Vertex *field_vertices = malloc(vertex_count * sizeof(Vertex));
	Vertex *piece_vertices = malloc(vertex_count * sizeof(Vertex));

	for (uint64_t segment = 0; segment < n * 2; segment++) {
		const float angle_a = (float)(segment + 0) / (float)(n * 2) * 2.0f * PI;
		const float angle_b = (float)(segment + 1) / (float)(n * 2) * 2.0f * PI;

		const float delta_a_x = -sinf(angle_a) * scale;
		const float delta_a_y = +cosf(angle_a) * scale;

		const float delta_b_x = -sinf(angle_b) * scale;
		const float delta_b_y = +cosf(angle_b) * scale;

		for (uint64_t a = 0; a < 4; a++) {
			for (uint64_t b = 0; b < 4; b++) {
				const uint64_t field_x = (segment % 2) ? (3 - b) : a + 4;
				const uint64_t field_y = 3 - ((segment % 2) ? a : b);
				const uint64_t field_z = segment >> 1;

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
					field_vertices[field_mesh->vertex_count++] = (Vertex){ \
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
					piece_vertices[piece_mesh->vertex_count++] = (Vertex){ \
						.x = X + (U - 0.5) * scale / 2, .y = Y + (V - 0.5) * scale / 2, \
						.u = U, .v = V, \
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

	glGenVertexArrays(1, &field_mesh->vao);
	glBindVertexArray(field_mesh->vao);

	glGenBuffers(1, &field_mesh->handle);
	glBindBuffer(GL_ARRAY_BUFFER, field_mesh->handle);
	glBufferData(GL_ARRAY_BUFFER, field_mesh->vertex_count * sizeof(Vertex), field_vertices, GL_STATIC_DRAW);
	free(field_vertices);

	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), 0);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, u));
	glEnableVertexAttribArray(1);
	glVertexAttribIPointer(2, 1, GL_INT, sizeof(Vertex), (void*)offsetof(Vertex, id));
	glEnableVertexAttribArray(2);


	glGenVertexArrays(1, &piece_mesh->vao);
	glBindVertexArray(piece_mesh->vao);

	glGenBuffers(1, &piece_mesh->handle);
	glBindBuffer(GL_ARRAY_BUFFER, piece_mesh->handle);
	glBufferData(GL_ARRAY_BUFFER, piece_mesh->vertex_count * sizeof(Vertex), piece_vertices, GL_STATIC_DRAW);
	free(piece_vertices);

	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), 0);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, u));
	glEnableVertexAttribArray(1);
	glVertexAttribIPointer(2, 1, GL_INT, sizeof(Vertex), (void*)offsetof(Vertex, id));
	glEnableVertexAttribArray(2);
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
		tile_x = clamp(3 - floor(b), 0, 7);
		tile_y = 3 - clamp(floor(a), 0, 3);
	} else {
		tile_x = clamp(floor(a) + 4, 0, 7);
		tile_y = 3 - clamp(floor(b), 0, 3);
	}

	return getId(tile_x, tile_y, tile_z);
}

void createEdge(Field *field, uint32_t a, uint32_t b) {
	if (getZ(a) != getZ(b)) {
		field->tiles[a].neighbors[North] = packIdAndDirection(b, South);
		field->tiles[b].neighbors[North] = packIdAndDirection(a, South);
	} else if (getX(a) < getX(b)) {
		field->tiles[a].neighbors[West] = packIdAndDirection(b, West);
		field->tiles[b].neighbors[East] = packIdAndDirection(a, East);
	} else {// (getY(a) < getY(b)) {
		field->tiles[a].neighbors[North] = packIdAndDirection(b, North);
		field->tiles[b].neighbors[South] = packIdAndDirection(a, South);
	}
}

void createField(Field *field) {
	for (uint64_t id = 0; id < field->num_players * 32; id++) {
		field->tiles[id] = (Tile){
			.id = id,
			.neighbors = {UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX},
		};
	}

	for (uint64_t z = 0; z < field->num_players; z++) {
		for (uint64_t y = 0; y < 3; y++) {
			for (uint64_t x = 0; x < 7; x++) {
				createEdge(field, getId(x, y, z), getId(x + 1, y, z));
				createEdge(field, getId(x, y, z), getId(x, y + 1, z));
			}
		}

		for (uint64_t x = 0; x < 7; x++) {
			createEdge(field, getId(x, 3, z), getId(x + 1, 3, z));
		}

		for (uint64_t y = 0; y < 3; y++) {
			createEdge(field, getId(7, y, z), getId(7, y + 1, z));
		}

		for (uint64_t x = 0; x < 4; x++) {
			createEdge(field, getId(x, 3, z), getId(7 - x, 3, (z + 1) % field->num_players));
		}

		for (uint64_t x = 0; x < 8; x++) {
			field->tiles[getId(x, 1, z)].figure = Pawn;
			field->tiles[getId(x, 1, z)].player = z;
			field->tiles[getId(x, 0, z)].player = z;
		}

		field->tiles[getId(0, 0, z)].figure = Rook;
		field->tiles[getId(1, 0, z)].figure = Knight;
		field->tiles[getId(2, 0, z)].figure = Bishop;
		field->tiles[getId(3, 0, z)].figure = Queen;
		field->tiles[getId(4, 0, z)].figure = King;
		field->tiles[getId(5, 0, z)].figure = Bishop;
		field->tiles[getId(6, 0, z)].figure = Knight;
		field->tiles[getId(7, 0, z)].figure = Rook;
	}
}

void markReachableNodes(Field *field, uint64_t start, uint64_t type, bool is_on_opposing_half, bool is_on_spawn_field) {
	#define isValidId(id) (id < field->num_players * 32)

	#define forward(pos) ({ \
		const uint64_t _pos = pos; \
		isValidId(extractId(_pos)) ? field->tiles[extractId(_pos)].neighbors[extractDirection(_pos)] : _pos; \
	})

	#define right(pos) ({ \
		const uint64_t _pos = pos; \
		isValidId(extractId(_pos)) ? field->tiles[extractId(_pos)].neighbors[(extractDirection(_pos) + 1) % 4] : _pos; \
	})

	#define left(pos) ({ \
		const uint64_t _pos = pos; \
		isValidId(extractId(_pos)) ? field->tiles[extractId(_pos)].neighbors[(extractDirection(_pos) + 3) % 4] : _pos; \
	})

	#define diagonalRight(pos) left(right(pos))
	#define diagonalLeft(pos) right(left(pos))

	#define markReachable(id) ({ \
		const uint64_t _id = id; \
		if (isValidId(_id)) { \
			field->tiles[_id].is_reachable = true; \
		} \
	})

	switch (type) {
		case None: break;
		case Pawn: {
			const uint64_t current = forward(packIdAndDirection(start, is_on_opposing_half ? South : North));
			markReachable(extractId(current));
			if (is_on_spawn_field) {
				markReachable(extractId(forward(current)));
			}
		} break;
		case Bishop: {
			for (uint64_t d = North; d <= West; d++) {
				uint64_t current = diagonalRight(packIdAndDirection(start, d));
				for (uint64_t i = 0; i < 8; i++) {
					markReachable(extractId(current));
					current = diagonalRight(current);
				}

				current = diagonalLeft(packIdAndDirection(start, d));
				for (uint64_t i = 0; i < 8; i++) {
					markReachable(extractId(current));
					current = diagonalLeft(current);
				}
			}
		} break;
		case Knight: {
			for (uint64_t d = North; d <= West; d++) {
				const uint64_t current = packIdAndDirection(start, d);
				markReachable(extractId(right(forward(forward(current)))));
				markReachable(extractId(left(forward(forward(current)))));
				markReachable(extractId(forward(right(forward(current)))));
				markReachable(extractId(forward(left(forward(current)))));
			}
		} break;
		case Rook: {
			for (uint64_t d = North; d <= West; d++) {
				uint64_t current = forward(packIdAndDirection(start, d));
				for (uint64_t i = 0; i < 8; i++) {
					markReachable(extractId(current));
					current = forward(current);
				}
			}
		} break;
		case Queen: {
			markReachableNodes(field, start, Bishop, is_on_opposing_half, is_on_spawn_field);
			markReachableNodes(field, start, Rook, is_on_opposing_half, is_on_spawn_field);
		} break;
		case King: {
			for (uint64_t d = North; d <= West; d++) {
				const uint64_t current = forward(packIdAndDirection(start, d));
				markReachable(extractId(current));
				markReachable(extractId(right(current)));
				markReachable(extractId(left(current)));
			}
		} break;
		default: break;
	}

	#undef isValidId
	#undef forward
	#undef right
	#undef left
	#undef diagonalRight
	#undef diagonalLeft
	#undef markReachable
}

float window_width = 1000, window_height = 1000;
void onFramebufferResized([[maybe_unused]] GLFWwindow *window, int width, int height) {
	glViewport(0, 0, width, height);
	window_width = width;
	window_height = height;
}

int main() {
	if(!glfwInit()) {
		panic("Failed to initialize GLFW\n");
	}

	atexit(glfwTerminate);

	glfwWindowHint(GLFW_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_VERSION_MINOR, 6);
	glfwWindowHint(GLFW_SAMPLES, 4);

	GLFWwindow* window = glfwCreateWindow(window_width, window_height, "chess", NULL, NULL);
	if (!window) {
		panic("Failed to open GLFW window\n");
	}

	glfwSetFramebufferSizeCallback(window, onFramebufferResized);

	glfwMakeContextCurrent(window);
	gladLoadGL();

	glEnable(GL_MULTISAMPLE);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glfwSwapInterval(1);

	const GLuint field_shader = loadProgram(2,
#if defined(SPIRV_SHADERS)
		GL_VERTEX_SHADER, ShaderFormat_SPIRV, ShaderSource_File, "build/shaders/field.vert.spv",
		GL_FRAGMENT_SHADER, ShaderFormat_SPIRV, ShaderSource_File, "build/shaders/field.frag.spv"
#else
		GL_VERTEX_SHADER, ShaderFormat_GLSL, ShaderSource_File, "shaders/field.vert",
		GL_FRAGMENT_SHADER, ShaderFormat_GLSL, ShaderSource_File, "shaders/field.frag"
#endif
	);

	const GLuint piece_shader = loadProgram(2,
#if defined(SPIRV_SHADERS)
		GL_VERTEX_SHADER, ShaderFormat_SPIRV, ShaderSource_File, "build/shaders/piece.vert.spv",
		GL_FRAGMENT_SHADER, ShaderFormat_SPIRV, ShaderSource_File, "build/shaders/piece.frag.spv"
#else
		GL_VERTEX_SHADER, ShaderFormat_GLSL, ShaderSource_File, "shaders/piece.vert",
		GL_FRAGMENT_SHADER, ShaderFormat_GLSL, ShaderSource_File, "shaders/piece.frag"
#endif
	);

	Field field = (Field) {
		.num_players = 3
	};
	const float scale = 0.5f / field.num_players;
	createField(&field);

	VertexBuffer field_mesh, piece_mesh;
	generateMeshes(field.num_players, scale, &field_mesh, &piece_mesh);

	ViewportInfoUniformData viewport_info_uniform_data = {
		.aspect_ratio = window_width / window_height
	};

	FieldShaderUniformData field_shader_uniform_data = {
		.num_players = field.num_players
	};

	GLuint viewport_info_uniform_buffer;
	glGenBuffers(1, &viewport_info_uniform_buffer);
	glBindBuffer(GL_UNIFORM_BUFFER, viewport_info_uniform_buffer);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(ViewportInfoUniformData), &viewport_info_uniform_data, GL_DYNAMIC_DRAW);

	GLuint field_shader_uniform_buffer;
	glGenBuffers(1, &field_shader_uniform_buffer);
	glBindBuffer(GL_UNIFORM_BUFFER, field_shader_uniform_buffer);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(FieldShaderUniformData), &field_shader_uniform_data, GL_DYNAMIC_DRAW);

	GLuint piece_shader_uniform_buffer;
	glGenBuffers(1, &piece_shader_uniform_buffer);
	glBindBuffer(GL_UNIFORM_BUFFER, piece_shader_uniform_buffer);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(Field), &field, GL_DYNAMIC_DRAW);

	int spritesheet_width = 0, spritesheet_height = 0, spritesheet_channels = 0;
	stbi_set_flip_vertically_on_load(true);
	unsigned char *spritesheet = stbi_load("spritesheet.png", &spritesheet_width, &spritesheet_height, &spritesheet_channels, 0);
	GLuint spritesheet_texture;
	glGenTextures(1, &spritesheet_texture);
	glBindTexture(GL_TEXTURE_2D, spritesheet_texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, spritesheet_width, spritesheet_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, spritesheet);
	glGenerateMipmap(GL_TEXTURE_2D);
	stbi_image_free(spritesheet);

	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);

	bool prev_m1 = GLFW_RELEASE;

	while(!glfwWindowShouldClose(window)) {
		glfwPollEvents();

		double xpos, ypos;
		glfwGetCursorPos(window, &xpos, &ypos);
		field.cursor_id = getTileUnderCursor((xpos / window_width * 2 - 1.0f) * (window_width / window_height), 1.0f - ypos / window_height * 2, field.num_players, scale);

		if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_1) == GLFW_RELEASE && prev_m1 == GLFW_PRESS) {
			if (field.tiles[field.cursor_id].is_reachable) {
				field.tiles[field.cursor_id].figure = field.tiles[field.selected_id].figure;
				field.tiles[field.cursor_id].player = field.tiles[field.selected_id].player;
				field.tiles[field.selected_id].figure = None;
				for (uint64_t i = 0; i < field.num_players * 32; i++) {
					field.tiles[i].is_reachable = 0;
				}
			} else if (field.tiles[field.cursor_id].figure != None) {
				for (uint64_t i = 0; i < field.num_players * 32; i++) {
					field.tiles[i].is_reachable = 0;
				}
				const bool is_on_opposing_half = field.tiles[field.cursor_id].player != getZ(field.cursor_id);
				markReachableNodes(&field, field.cursor_id, field.tiles[field.cursor_id].figure, is_on_opposing_half, false);
				field.selected_id = field.cursor_id;
			}
		}

		prev_m1 = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_1);

		glClear(GL_COLOR_BUFFER_BIT);

		viewport_info_uniform_data.aspect_ratio = window_width / window_height;
		glBindBuffer(GL_UNIFORM_BUFFER, viewport_info_uniform_buffer);
		glBufferData(GL_UNIFORM_BUFFER, sizeof(ViewportInfoUniformData), &viewport_info_uniform_data, GL_DYNAMIC_DRAW);

		glBindBuffer(GL_UNIFORM_BUFFER, piece_shader_uniform_buffer);
		glBufferData(GL_UNIFORM_BUFFER, sizeof(Field), &field, GL_DYNAMIC_DRAW);

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

		glfwSwapBuffers(window);
	}

	glfwDestroyWindow(window);

	return 0;
}
