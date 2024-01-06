#version 460 core

layout (binding = 1, std140) uniform uniform_data {
	int num_players;
	int player;
};

layout (location = 1) in vec2 uv;
layout (location = 2) flat in int id;

layout (location = 0) out vec4 out_color;

const vec4 border_colors[] = vec4[](
	vec4(1.00, 0.98, 0.97, 1.0),
	vec4(0.15, 0.19, 0.19, 1.0),
	vec4(0.92, 0.28, 0.19, 1.0),
	vec4(0.35, 0.45, 0.95, 1.0),
	vec4(0.12, 0.39, 0.21, 1.0)
);

const vec4 tile_colors[] = vec4[](
	vec4(0.28, 0.20, 0.14, 1.0),
	vec4(1.00, 0.87, 0.70, 1.0)
);

void main() {
	const vec2 uv2 = abs(uv * 2 - 1);
	const float sharp_dist_to_center = smoothstep(0.95, 0.97, max(uv2.x, uv2.y));

	const int x = id & 7, y = (id>>3) & 3, z = id>>5;
	const int parity = (x + y) % 2;

	const vec4 border_color = border_colors[(z + player) % num_players];
	const vec4 tile_color = tile_colors[parity];

	out_color = mix(tile_color, border_color, sharp_dist_to_center);
}
