#include "gl.hpp"
#include "io.hpp"

#include <fstream>
#include <vector>

std::optional<GLuint> loadShader(std::string_view name, std::span<uint8_t> source, GLenum type, bool is_spirv) {
	if (source.empty()) {
		return {};
	}

	GLuint shader = glCreateShader(type);

	if (is_spirv) {
		glShaderBinary(1, &shader, GL_SHADER_BINARY_FORMAT_SPIR_V_ARB, source.data(), source.size());
		glSpecializeShader(shader, "main", 0, NULL, NULL);
	} else {
		const uint8_t *tmp = source.data();
		glShaderSource(shader, 1, (const char**)&tmp, NULL);
		glCompileShader(shader);
	}

	int success;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

	if(!success) {
		char info_log[2048];
		glGetShaderInfoLog(shader, 2048, NULL, info_log);
		eprintln("Failed to compile shader {}: \n{}", name, info_log);
		return {};
	}

	return shader;
}

std::optional<GLuint> loadProgram(std::string_view name, std::initializer_list<ShaderStage> stages) {
	const GLuint program = glCreateProgram();
	std::vector<GLuint> shaders;

	for (const auto &stage : stages) {
		std::string content;
		if (stage.load_from_file) {
			std::ifstream file(stage.path_or_data);
			content = std::string{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
		} else {
			content = stage.path_or_data;
		}

		if (const auto shader = loadShader(name, std::span<uint8_t>((uint8_t*)content.c_str(), content.size()), stage.stage, stage.is_spirv)) {
			shaders.push_back(shader.value());
			glAttachShader(program, shader.value());
		} else {
			return {};
		}
	}

	glLinkProgram(program);

	for (const GLuint shader : shaders) {
		glDeleteShader(shader);
	}

	int success;
	glGetProgramiv(program, GL_LINK_STATUS, &success);
	if(!success) {
		char info_log[2048];
		glGetProgramInfoLog(program, 2048, NULL, info_log);
		eprintln("Failed to link shader program {}: \n{}", name, info_log);
		return {};
	}

	return program;
}
