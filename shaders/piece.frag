#version 460 core

#define MAX_PLAYERS 10

#define None 0
#define Pawn 1
#define Bishop 2
#define Knight 3
#define Rook 4
#define Queen 5
#define King 6

struct Tile {
	ivec4 neighbors;
	int id;
	int figure;
	int player;
	bool is_reachable;
};

layout (binding = 1, std140) uniform Field {
	Tile tiles[32 * MAX_PLAYERS];
	int num_players;
	int cursor_id;
};

layout (binding = 0) uniform sampler2D spritesheet;

layout (location = 1) in vec2 uv;
layout (location = 2) flat in int id;

layout (location = 0) out vec4 out_color;

const vec4 playerColors[] = vec4[](
	vec4(0, 1, 0, 1),
	vec4(0, 0, 1, 1),
	vec4(1, 0, 0, 1)
);

const vec4 selectionColors[] = vec4[](
	vec4(0.3, 0.9, 0.2, 1.0),
	vec4(1.0, 0.3, 0.2, 1.0)
);

float sdfCircle(vec2 p, float radius) {
	return length(p) - radius;
}

float sdfBox(vec2 p, vec2 half_size) {
	vec2 edge_dist = abs(p) - half_size;
	float outside_dist = length(max(edge_dist, 0));
	float inside_dist = min(max(edge_dist.x, edge_dist.y), 0);
	return outside_dist + inside_dist;
}

vec4 getFigureColor(bool parity, int figure, int player) {
	return texture(spritesheet, uv / vec2(8, 1) + vec2(float(figure) / 8, 0)) * playerColors[player];
}

vec4 getSelectionColor(bool parity, int figure) {
	const float dist = sdfCircle(uv - 0.5, 0.35);

	const vec4 border_color = vec4(vec3(mix(0.9, 0.1, parity)), 1);
	const vec4 fill_color = id == cursor_id ? vec4(0.2, 0.3, 0.9, 1.0) : selectionColors[int(figure != 0)];

	const float inside = smoothstep(0.05, 0.0, dist);
	const float on_border = smoothstep(0.05, 0.0, dist - 0.07);

	return mix(mix(vec4(0), border_color * fill_color.a, on_border), fill_color, inside);
}

void main() {
	const int x = id & 7, y = (id>>3) & 3, z = id>>5;
	const bool parity = ((x + y) % 2) == 1;

	const int figure = tiles[id].figure;
	const int player = tiles[id].player;
	const bool selected = tiles[id].is_reachable || id == cursor_id;

	const vec4 figureColor = getFigureColor(parity, figure, player) * float(figure != None);
	const vec4 selectionColor = getSelectionColor(parity, figure);
	out_color = mix(figureColor, selectionColor, selectionColor.a * float(selected));
}
