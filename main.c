#include <math.h>
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

#define PI 3.141592654f

#define getId(x, y, z) (((z)<<5) | ((y)<<3) | (x))
#define getX(id) ((id) & 0b00000111)
#define getY(id) (((id)>>3) & 0b00000011)
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

typedef struct {
	uint8_t *data;
	uint64_t size;
} Buffer;

typedef struct {
	float x, y;
	float u, v;
	uint8_t idx, idy, idz;
} Vertex;

typedef struct {
	GLuint handle;
	uint64_t vertex_count, vertex_size;
} VertexBuffer;

typedef struct {
	uint64_t id;
	uint64_t neighbors[4];
} Node;

enum Direction {
	North,
	East,
	South,
	West,
};

enum FigureType {
	None,
	Pawn,
	Bishop,
	Knight,
	Rook,
	Queen,
	King,
};

noreturn void panic(const char *msg) {
	fputs(msg, stderr);
	exit(EXIT_FAILURE);
}

Buffer loadFile(const char *path) {
	FILE *file = fopen(path, "r");
	if (!file) {
		panic("couldn't load shader\n");
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

GLuint loadShader(const char *path, GLenum type, bool is_spirv) {
	const Buffer source = loadFile(path);
	GLuint shader = glCreateShader(type);

	if (is_spirv) {
		glShaderBinary(1, &shader, GL_SHADER_BINARY_FORMAT_SPIR_V_ARB, source.data, source.size);
		glSpecializeShader(shader, "main", 0, NULL, NULL);
	} else {
		glShaderSource(shader, 1, (const char**)&source.data, NULL);
		glCompileShader(shader);

		int success;
		glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

		if(!success) {
			char infoLog[512];
			glGetShaderInfoLog(shader, 512, NULL, infoLog);
			puts(infoLog);
		}
	}

	free(source.data);
	return shader;
}

VertexBuffer generateMesh(uint64_t n, float scale) {
	GLuint vertex_buffer;
	glGenBuffers(1, &vertex_buffer);
	glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);

	const uint64_t vertex_count = (n * 32) * 6;
	Vertex *vertices = malloc(vertex_count * sizeof(Vertex));
	uint64_t vertex_index = 0;
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

				#define addVertex(X, Y, U, V) \
					vertices[vertex_index++] = (Vertex){ \
						.x = X, .y = Y, \
						.u = U, .v = V, \
						.idx = field_x, \
						.idy = field_y, \
						.idz = field_z, \
					};

				addVertex(x1, y1, 1, 0);
				addVertex(x0, y0, 0, 0);
				addVertex(x2, y2, 0, 1);

				addVertex(x2, y2, 0, 1);
				addVertex(x3, y3, 1, 1);
				addVertex(x1, y1, 1, 0);

				#undef addVertex
			}
		}
	}

	glBufferData(GL_ARRAY_BUFFER, vertex_count * sizeof(Vertex), vertices, GL_STATIC_DRAW);
	free(vertices);

	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), 0);
	glEnableVertexAttribArray(0);

	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, u));
	glEnableVertexAttribArray(1);

	glVertexAttribIPointer(2, 3, GL_BYTE, sizeof(Vertex), (void*)offsetof(Vertex, idx));
	glEnableVertexAttribArray(2);

	return (VertexBuffer){
		.handle = vertex_buffer,
		.vertex_count = vertex_count,
		.vertex_size = sizeof(Vertex)
	};
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

void createEdge(Node *a, Node *b) {
	if (getZ(a->id) != getZ(b->id)) {
		a->neighbors[North] = packIdAndDirection(b->id, South);
		b->neighbors[North] = packIdAndDirection(a->id, South);
	} else if (getX(a->id) < getX(b->id)) {
		a->neighbors[West] = packIdAndDirection(b->id, West);
		b->neighbors[East] = packIdAndDirection(a->id, East);
	} else {// (getY(a->id) < getY(b->id)) {
		a->neighbors[North] = packIdAndDirection(b->id, North);
		b->neighbors[South] = packIdAndDirection(a->id, South);
	}
}

Node* createField(uint64_t n) {
	Node *field = malloc(n * 32 * sizeof(Node));

	for (uint64_t id = 0; id < n * 32; id++) {
		field[id] = (Node){
			.id = id,
			.neighbors = {UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX},
		};
	}

	for (uint64_t z = 0; z < n; z++) {
		for (uint64_t y = 0; y < 3; y++) {
			for (uint64_t x = 0; x < 7; x++) {
				createEdge(field + getId(x, y, z), field + getId(x + 1, y, z));
				createEdge(field + getId(x, y, z), field + getId(x, y + 1, z));
			}
		}

		for (uint64_t x = 0; x < 7; x++) {
			createEdge(field + getId(x, 3, z), field + getId(x + 1, 3, z));
		}

		for (uint64_t y = 0; y < 3; y++) {
			createEdge(field + getId(7, y, z), field + getId(7, y + 1, z));
		}

		for (uint64_t x = 0; x < 4; x++) {
			createEdge(field + getId(x, 3, z), field + getId(7 - x, 3, (z + 1) % n));
		}
	}

	return field;
}

void markReachableNodes(Node *field, uint64_t n, uint64_t start, uint64_t type, bool is_on_opposing_half, bool is_on_spawn_field, uint32_t *reachable) {
	#define isValidId(id) (id < n * 32)

	#define forward(pos) ({ \
		const uint64_t _pos = pos; \
		isValidId(extractId(_pos)) ? field[extractId(_pos)].neighbors[extractDirection(_pos)] : _pos; \
	})

	#define right(pos) ({ \
		const uint64_t _pos = pos; \
		isValidId(extractId(_pos)) ? field[extractId(_pos)].neighbors[(extractDirection(_pos) + 1) % 4] : _pos; \
	})

	#define left(pos) ({ \
		const uint64_t _pos = pos; \
		isValidId(extractId(_pos)) ? field[extractId(_pos)].neighbors[(extractDirection(_pos) + 3) % 4] : _pos; \
	})

	#define diagonalRight(pos) left(right(pos))
	#define diagonalLeft(pos) right(left(pos))

	#define markReachable(id) ({ \
		const uint64_t _id = id; \
		if (isValidId(_id)) { \
			reachable[getZ(_id)] |= 1<<(_id & 31); \
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
			markReachableNodes(field, n, start, Bishop, is_on_opposing_half, is_on_spawn_field, reachable);
			markReachableNodes(field, n, start, Rook, is_on_opposing_half, is_on_spawn_field, reachable);
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

int main() {
	if(!glfwInit()) {
		panic("Failed to initialize GLFW\n");
	}

	atexit(glfwTerminate);

	glfwWindowHint(GLFW_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_VERSION_MINOR, 6);
	glfwWindowHint(GLFW_SAMPLES, 4);

	float window_width = 1000, window_height = 1000;
	GLFWwindow* window = glfwCreateWindow(window_width, window_height, "chess", NULL, NULL);
	if (!window) {
		panic("Failed to open GLFW window\n");
	}

	glfwSetWindowOpacity(window, 0.0f);

	glfwMakeContextCurrent(window);
	gladLoadGL();

	glfwSwapInterval(1);

#if defined(SPIRV_SHADERS)
	const GLuint vertex_shader = loadShader("build/vert.spv", GL_VERTEX_SHADER, true);
	const GLuint fragment_shader = loadShader("build/frag.spv", GL_FRAGMENT_SHADER, true);
#else
	const GLuint vertex_shader = loadShader("main.vert", GL_VERTEX_SHADER, false);
	const GLuint fragment_shader = loadShader("main.frag", GL_FRAGMENT_SHADER, false);
#endif

	const GLuint shader_program = glCreateProgram();
	glAttachShader(shader_program, vertex_shader);
	glAttachShader(shader_program, fragment_shader);
	glLinkProgram(shader_program);

	int success;
	glGetProgramiv(shader_program, GL_LINK_STATUS, &success);
	if(!success) {
		char info_log[512];
		glGetProgramInfoLog(shader_program, 512, NULL, info_log);
		puts(info_log);
	}

	glDeleteShader(vertex_shader);
	glDeleteShader(fragment_shader);

	glUseProgram(shader_program);

	const uint32_t n = 3;
	const float scale = 0.14f;

	const VertexBuffer field_mesh = generateMesh(n, scale);
	Node *field = createField(n);
	uint32_t *reachable = malloc(sizeof(uint32_t) * n);

	const uint64_t uniform_size = (n + 1) * 4;
	uint32_t *uniform_data = malloc(uniform_size * sizeof(int));
	memset(uniform_data, 0, uniform_size * sizeof(int));
	uniform_data[0] = n;

	GLuint uniform_buffer;
	glGenBuffers(1, &uniform_buffer);
	glBindBuffer(GL_UNIFORM_BUFFER, uniform_buffer);
	glBufferData(GL_UNIFORM_BUFFER, uniform_size * sizeof(int), uniform_data, GL_DYNAMIC_DRAW);
	glBindBufferBase(GL_UNIFORM_BUFFER, 0, uniform_buffer);

	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glEnable(GL_MULTISAMPLE);

	uint64_t piece = None;
	uint64_t prev_key = GLFW_RELEASE;
	while(!glfwWindowShouldClose(window)) {
		glfwPollEvents();

		if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_RELEASE && prev_key == GLFW_PRESS) {
			piece = (piece + 1) % 7;
		}
		prev_key = glfwGetKey(window, GLFW_KEY_SPACE);

		glClear(GL_COLOR_BUFFER_BIT);

		double xpos, ypos;
		glfwGetCursorPos(window, &xpos, &ypos);
		uint64_t tile = getTileUnderCursor(xpos / window_width * 2 - 1.0f, 1.0f - ypos / window_height * 2, n, scale);

		for (uint64_t i = 0; i < n; i++) {
			reachable[i] = 0;
		}

		markReachableNodes(field, n, tile, piece, false, false, reachable);

		memset(&uniform_data[4], 0, n * 4 * sizeof(int));
		for (uint64_t i = 0; i < n; i++) {
			uniform_data[(i + 1) * 4] = reachable[i];
		}

		uniform_data[(getZ(tile) + 1) * 4] |= 1<<(tile & 31);
		glBufferData(GL_UNIFORM_BUFFER, uniform_size * sizeof(int), uniform_data, GL_DYNAMIC_DRAW);

		glDrawArrays(GL_TRIANGLES, 0, field_mesh.vertex_count);

		glfwSwapBuffers(window);
	}

	free(field);
	free(reachable);
	free(uniform_data);
	glfwDestroyWindow(window);

	return 0;
}
