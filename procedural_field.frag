#version 460 core

layout(binding = 0) uniform uniform_data {
	float time;
};

layout (location = 1) in vec2 uv;

layout (location = 0) out vec4 color;

#define PI 3.141592654f

vec2 rotateZ(vec2 v, float a) {
	const float s = sin(a), c = cos(a);
	return mat2(c, s, -s, c) * v;
}

float polarAngle(vec2 v) {
	return atan(v.y / v.x) / (2.0f * PI) + 0.25f + 0.5f * float(v.x < 0.0f);
}

float segmentIndex(float segments, float angle) {
	return floor(angle * segments);
}

vec3 hsv2rgb(float hue, float saturation, float value) {
	const vec4 K = vec4(1.0f, 2.0f / 3.0f, 1.0f / 3.0f, 3.0f);
	const vec3 p = abs(fract(vec3(hue) + K.xyz) * 6.0f - K.www);
	return value * mix(K.xxx, clamp(p - K.xxx, 0.0f, 1.0f), saturation);
}

vec2 segmentTangent(float segment_count, float segment_index, float offset) {
	const float angle = (offset / segment_count + 0.25f + segment_index) * 2.0f * PI;
	return rotateZ(vec2(0.0f, 1.0f), angle);
}

vec3 segmentStripe(vec2 pos, float segment_count, float segment_index, float offset, float scale) {
	const vec2 tangent_dir = segmentTangent(segment_count, segment_index, offset);
	const float dist = dot(tangent_dir, pos) * scale;
	const float on_border = float(fract(dist + 0.05f) < 0.1f);
	const float tile_dist = floor(abs(dist));
	const float dist_to_tile_center = fract(dist) - 0.5f;
	return vec3(on_border, tile_dist, dist_to_tile_center);
}

void main() {
	const float n = mix(2.0f, 5.0f, abs(sin(time / 10.0f)));
	const float rot = time / 3.0f;
	const float scale = 10.0f;

	const float n2 = n * 2.0f;
	const vec2 uv2 = rotateZ(uv, rot);
	const float angle = polarAngle(uv2);

	const float half_segment_index = segmentIndex(n2, angle);
	const float relative_index = half_segment_index / n2;
	const vec3 stripe_a = segmentStripe(uv2, n2, relative_index, 0.0f, scale);
	const vec3 stripe_b = segmentStripe(uv2, n2, relative_index, 1.0f, scale);

	const float on_tile_border = clamp(stripe_a.x + stripe_b.x, 0.0f, 1.0f);
	const float tile_parity = clamp(mod(stripe_a.y + stripe_b.y + half_segment_index, 2.0f), 0.0f, 1.0f);
	const float dist_to_tile_center = length(vec2(stripe_a.z, stripe_b.z));

	const vec3 inside_color = vec3(abs(float(dist_to_tile_center < 0.1f) - tile_parity));
	const vec3 segment_color = hsv2rgb(segmentIndex(n, angle) / n, 1.0f, 1.0f);

	const float inside_field = float(max(stripe_a.y, stripe_b.y) < 4.0f);
	color = vec4(mix(inside_color, segment_color, on_tile_border) * inside_field, 1.0f);
}
