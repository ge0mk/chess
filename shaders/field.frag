#version 460 core

layout (binding = 1, std140) uniform uniform_data {
	int num_players;
};

layout (location = 1) in vec2 uv;
layout (location = 2) flat in int id;

layout (location = 0) out vec4 out_color;

vec3 hsv2rgb(float hue, float saturation, float value) {
	const vec4 K = vec4(1.0f, 2.0f / 3.0f, 1.0f / 3.0f, 3.0f);
	const vec3 p = abs(fract(vec3(hue) + K.xyz) * 6.0f - K.www);
	return value * mix(K.xxx, clamp(p - K.xxx, 0.0f, 1.0f), saturation);
}

void main() {
	const vec2 uv2 = abs(uv * 2 - 1);
	const float sharp_dist_to_center = smoothstep(0.91, 0.95, max(uv2.x, uv2.y));

	const int x = id & 7, y = (id>>3) & 3, z = id>>5;
	const int parity = (x + y) % 2;

	const vec3 border_color = hsv2rgb(float(z) / float(num_players), 1.0f, 1.0f);
	const vec3 tile_color = vec3(float(parity));

	out_color = vec4(mix(tile_color, border_color, sharp_dist_to_center), 1.0f);
}
