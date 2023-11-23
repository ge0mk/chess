#version 460 core

layout (binding = 0, std140) uniform uniform_data {
	int num_players;
	int fields_marked[8];
};

layout (location = 1) in vec2 uv;
layout (location = 2) flat in ivec3 id;

layout (location = 0) out vec4 out_color;

vec3 hsv2rgb(float hue, float saturation, float value) {
	const vec4 K = vec4(1.0f, 2.0f / 3.0f, 1.0f / 3.0f, 3.0f);
	const vec3 p = abs(fract(vec3(hue) + K.xyz) * 6.0f - K.www);
	return value * mix(K.xxx, clamp(p - K.xxx, 0.0f, 1.0f), saturation);
}

vec3 select2(vec3 a, vec3 b, bool take_b) {
	return mix(a, b, float(take_b));
}

vec3 select3(vec3 a, vec3 b, vec3 c, bool take_b, bool take_c) {
	return select2(select2(a, b, take_b), c, take_c);
}

void main() {
	const vec2 uv2 = abs(uv * 2 - 1);
	const float sharp_dist_to_center = max(uv2.x, uv2.y);
	const float smooth_dist_to_center = length(uv2);

	const int parity = (id.x + id.y) % 2;
	const int id2 = (id.z<<5) | (id.y<<3) | id.x;
	const uint marked = (fields_marked[id.z] >> ((id.y<<3) | id.x)) & 1;

	const vec3 border_color = hsv2rgb(float(id.z) / float(num_players), 1.0f, 1.0f);
	const vec3 tile_color = vec3(float(parity));
	const vec3 selection_color = mix(tile_color, vec3(0.2f, 0.7f, 0.2f), float(marked != 0));

	out_color = vec4(select3(tile_color, selection_color, border_color, smooth_dist_to_center < 0.4, sharp_dist_to_center > 0.95), 1.0f);
}
