#version 460 core

#define MAX_PLAYERS 10

#define None 0
#define Pawn 1
#define Bishop 2
#define Knight 3
#define Rook 4
#define Queen 5
#define King 6

layout (binding = 1, std140) uniform Field {
	ivec4 tiles[8 * MAX_PLAYERS];
	int cursor_id;
	int num_players;
	int selected_id;
	int player;
};

layout (binding = 0) uniform sampler2D spritesheet;

layout (location = 1) in vec2 uv;
layout (location = 2) flat in int id;

layout (location = 0) out vec4 out_color;

const vec4 fill_colors[] = vec4[](
	vec4(1.00, 0.98, 0.97, 1.0),
	vec4(0.15, 0.19, 0.19, 1.0),
	vec4(0.92, 0.28, 0.19, 1.0),
	vec4(0.35, 0.45, 0.95, 1.0),
	vec4(0.12, 0.39, 0.21, 1.0)
);

const vec4 border_colors[] = vec4[](
	vec4(0.15, 0.19, 0.19, 1.0),
	vec4(1.00, 0.98, 0.97, 1.0),
	vec4(0.29, 0.05, 0.04, 1.0),
	vec4(0.12, 0.20, 0.45, 1.0),
	vec4(0.35, 0.85, 0.45, 1.0)
);

const vec4 selection_colors[] = vec4[](
	vec4(0.18, 0.86, 0.14, 1.0),	// can move to empty field
	vec4(0.86, 0.18, 0.14, 1.0),	// can capture
	vec4(0.14, 0.18, 0.86, 1.0)		// under cursor
);

const vec4 tile_colors[] = vec4[](
	vec4(0, 0, 0, 1.0),
	vec4(1, 1, 1, 1.0)
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
	const vec4 base = texture(spritesheet, uv / vec2(8, 1) + vec2(float(figure) / 8, 0));
	return (fill_colors[player] * base.r + border_colors[player] * base.g) * base.a;
}

vec4 getSelectionColor(bool parity, int figure, bool is_cursor) {
	const float dist = sdfCircle(uv - 0.5, 0.2);

	const vec4 border_color = tile_colors[int(!parity)];
	const vec4 fill_color = is_cursor ? selection_colors[2] : selection_colors[int(figure != 0)];

	const float inside = smoothstep(0.03, 0.0, dist);
	const float on_border = smoothstep(0.03, 0.0, dist - 0.04);

	return mix(mix(vec4(0), border_color * fill_color.a, on_border), fill_color, inside);
}

void main() {
	const int x = id & 7, y = (id>>3) & 3, z = id>>5;
	const int id2 = (id + (player<<5)) % (num_players<<5);
	const bool parity = ((x + y) % 2) == 1;

	const int figure = (tiles[id2 / 4][id2 % 4] >> 0) & 255;
	const int player = (tiles[id2 / 4][id2 % 4] >> 8) & 255;
	const bool is_cursor = id == cursor_id;
	const bool selected = (((tiles[id2 / 4][id2 % 4] >> 16) & 255) != 0) || is_cursor;

	const vec4 figure_color = getFigureColor(parity, figure, player) * float(figure != None);
	const vec4 selection_color = getSelectionColor(parity, figure, is_cursor);
	out_color = mix(figure_color, selection_color, selection_color.a * float(selected));
}
