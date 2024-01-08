#version 460 core

#define MAX_PLAYERS 8

#define None 0
#define Pawn 1
#define Bishop 2
#define Knight 3
#define Rook 4
#define Queen 5
#define King 6

layout (location = 1) in vec2 uv;
layout (location = 2) flat in int id;

layout (location = 0) out vec4 out_color;

layout (binding = 1, std140) uniform Field {
	ivec4 tiles[8 * MAX_PLAYERS];
	int cursor_id;
	int num_players;
	int selected_id;
	int player_offset;
};

layout (binding = 0) uniform sampler2D palette;
layout (binding = 1) uniform sampler2D spritesheet;

void main() {
	const int x = id & 7, y = (id>>3) & 3, z = id>>5;
	const int id2 = (id + (player_offset<<5)) % (num_players<<5);
	const bool parity = ((x + y) % 2) == 1;

	const int figure = (tiles[id2 / 4][id2 % 4] >> 0) & 255;
	const int owner = (tiles[id2 / 4][id2 % 4] >> 8) & 255;
	const bool is_cursor = id2 == cursor_id;
	const bool selected = (((tiles[id2 / 4][id2 % 4] >> 16) & 255) != 0) || is_cursor;

	const vec4 figure_base = texture(spritesheet, uv / vec2(8, 1) + vec2(float(figure) / 8, 0));
	const vec4 figure_fill = texture(palette, vec2(float(owner) / MAX_PLAYERS, 0.375));
	const vec4 figure_border = texture(palette, vec2(float(owner) / MAX_PLAYERS, 0.625));
	const vec4 figure_color = (figure_fill * figure_base.r + figure_border * figure_base.g) * figure_base.a;

	const vec4 selection_base = texture(spritesheet, uv / vec2(8, 1) + vec2(0.875, 0));
	const vec4 selection_border = texture(palette, vec2((float(!parity) + 2) / MAX_PLAYERS, 0.125));
	const vec4 selection_fill = texture(palette, vec2(float((is_cursor ? 2 : int(figure != 0)) + 4) / MAX_PLAYERS, 0.125));
	const vec4 selection_color = (selection_fill * selection_base.r + selection_border * selection_base.g) * selection_base.a;

	out_color = mix(figure_color, selection_color, selection_color.a * float(selected));
}
