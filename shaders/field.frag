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
	ivec4 tiles[8 * MAX_PLAYERS + 1];
	int cursor_id;
	int num_players;
	int selected_id;
	int player_offset;
};

layout (binding = 0) uniform sampler2D palette;
layout (binding = 1) uniform sampler2D spritesheet;

void main() {
	const int x = id & 7, y = (id>>3) & 3, z = (((id>>5) & 3) + player_offset) % num_players;
	const bool parity = ((x + y) % 2) == 1;
	const bool tile_background = false;
	const vec2 uv2 = abs(uv - 0.5);

	if ((id & 0x200) != 0) {
		const vec4 border_color = texture(palette, vec2(float(z) / MAX_PLAYERS, 0.875));
		const vec4 tile_color = texture(palette, vec2(float(parity) / MAX_PLAYERS, 0.125));
		out_color = mix(tile_color, border_color, smoothstep(0.475, 0.48, max(uv2.x, uv2.y)));
	} else {
		const int id2 = id >= 32 * MAX_PLAYERS ? id : (id + (player_offset<<5)) % (num_players<<5);

		const int tile = tiles[id2 / 4][id2 % 4];
		const int figure = (tile >> 0) & 255;
		const int owner = (tile >> 8) & 255;
		const bool is_cursor = id2 == cursor_id;
		const bool selected = (((tile >> 16) & 255) != 0) || is_cursor;

		const vec4 figure_base = texture(spritesheet, uv / vec2(8, 1) + vec2(float(figure) / 8, 0));
		const vec4 figure_fill = texture(palette, vec2(float(owner) / MAX_PLAYERS, 0.375));
		const vec4 figure_border = texture(palette, vec2(float(owner) / MAX_PLAYERS, 0.625));
		const vec4 figure_color = (figure_fill * figure_base.r + figure_border * figure_base.g) * figure_base.a;

		if (id >= MAX_PLAYERS * 32) {
			const vec4 promotion_light = texture(palette, vec2(0.125, 0.125));
			const vec4 promotion_dark = is_cursor ? texture(palette, vec2(0.75, 0.125)) : texture(palette, vec2(0.0, 0.125));
			const float on_border = smoothstep(0.6, 0.62, length(uv - vec2(0.5, 0.5)));
			const float outside = smoothstep(0.7, 0.72, length(uv - vec2(0.5, 0.5)));
			const vec4 background = mix(promotion_light, promotion_dark, on_border) * (1.0f - outside);

			out_color = mix(background, figure_color, figure_color.a * float(uv2.x <= 0.5 && uv2.y <= 0.5));
		} else {
			const vec4 selection_base = texture(spritesheet, uv / vec2(8, 1) + vec2(0.875, 0));
			const vec4 selection_border = texture(palette, vec2((float(!parity) + 2) / MAX_PLAYERS, 0.125));
			const vec4 selection_fill = texture(palette, vec2(float((is_cursor ? 2 : int(figure != 0)) + 4) / MAX_PLAYERS, 0.125));

			const vec4 selection_color = (selection_fill * selection_base.r + selection_border * selection_base.g) * selection_base.a;
			out_color = mix(figure_color, selection_color, selection_color.a * float(selected));
		}
	}
}
