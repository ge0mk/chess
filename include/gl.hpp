#pragma once

#include "SDL_surface.h"
#include "glad.h"

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>

struct ShaderStage {
	std::string path_or_data;
	GLenum stage;
	bool load_from_file;
	bool is_spirv;
};

std::optional<GLuint> loadShader(std::string_view name, std::span<uint8_t> source, GLenum type, bool is_spirv = false);
std::optional<GLuint> loadProgram(std::string_view name, std::initializer_list<ShaderStage> stages);

template <typename T>
struct Buffer {
	GLuint handle;

	static inline Buffer<T> create() {
		Buffer<T> b;
		glGenBuffers(1, &b.handle);
		return b;
	}

	inline void destroy() {
		glDeleteBuffers(1, &handle);
	}

	inline void bind(GLenum target) {
		glBindBuffer(target, handle);
	}

	inline void data(GLenum target, std::span<T> data, GLenum usage) {
		glBindBuffer(target, handle);
		glBufferData(target, data.size() * sizeof(T), data.data(), usage);
	}

	inline void subData(GLenum target, GLintptr offset, std::span<T> data) {
		glBindBuffer(target, handle);
		glBufferSubData(target, offset, data.size() * sizeof(T), data.data());
	}
};

struct Texture {
	GLuint handle;

	static inline Texture create(SDL_Surface *surface) {
		Texture t;
		glGenTextures(1, &t.handle);
		glBindTexture(GL_TEXTURE_2D, t.handle);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, surface->w, surface->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, surface->pixels);
		glGenerateMipmap(GL_TEXTURE_2D);
		return t;
	}

	inline void destroy() {
		glDeleteTextures(1, &handle);
	}

	inline void bind() {
		glBindTexture(GL_TEXTURE_2D, handle);
	}
};
