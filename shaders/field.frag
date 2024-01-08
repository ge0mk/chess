#version 460 core

#define MAX_PLAYERS 8

layout (location = 1) in vec2 uv;
layout (location = 2) flat in int id;

layout (location = 0) out vec4 out_color;

layout (binding = 1, std140) uniform uniform_data {
	int num_players;
	int player;
};

layout (binding = 0) uniform sampler2D palette;

void main() {
	const vec2 uv2 = abs(uv * 2 - 1);
	const float sharp_dist_to_center = smoothstep(0.95, 0.97, max(uv2.x, uv2.y));

	const int x = id & 7, y = (id>>3) & 3, z = id>>5;
	const int parity = (x + y) % 2;

	const vec4 border_color = texture(palette, vec2(float((z + player) % num_players) / MAX_PLAYERS, 0.875));
	const vec4 tile_color = texture(palette, vec2(float(parity) / MAX_PLAYERS, 0.125));

	out_color = mix(tile_color, border_color, sharp_dist_to_center);
}
