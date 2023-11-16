#version 460 core

layout (location = 0) in vec2 pos;
layout (location = 1) in vec2 uv;
layout (location = 2) in ivec3 id;

layout (location = 1) out vec2 out_uv;
layout (location = 2) flat out ivec3 out_id;

void main() {
	gl_Position = vec4(pos, 0.0f, 1.0);
	out_uv = uv;
	out_id = id;
}
