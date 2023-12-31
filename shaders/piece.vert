#version 460 core

layout (location = 0) in vec2 pos;
layout (location = 1) in vec2 uv;
layout (location = 2) in int id;

layout (location = 1) out vec2 out_uv;
layout (location = 2) flat out int out_id;

layout (binding = 0, std140) uniform aspect_ratio_uniform_data {
	float aspect_ratio;
};

void main() {
	gl_Position = vec4(pos / vec2(aspect_ratio, 1.0f), 0.1f, 1.0);
	out_uv = uv;
	out_id = id;
}
