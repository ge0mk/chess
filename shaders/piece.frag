#version 460 core

layout (binding = 1, std140) uniform uniform_data {
	ivec4 field[32];
	int cursor_id;
	int num_players;

	// [
	//	(piece0, piece1, piece2, selected),
	//	(player0, player1, player2, player3)
	// ]
};

layout (location = 1) in vec2 uv;
layout (location = 2) flat in int id;

layout (location = 0) out vec4 out_color;

#define Empty 0
#define Pawn 1
#define Bishop 2
#define Knight 3
#define Rook 4
#define Queen 5
#define King 6
#define Select 8 // can be enabled for all other pieces

const vec4 playerColors[] = vec4[](
	vec4(0, 0, 0, 1),
	vec4(1, 1, 1, 1),
	vec4(1, 0, 0, 1),
	vec4(0, 0, 1, 1),
	vec4(0, 1, 0, 1)
);

const vec4 selectionColors[] = vec4[](
	vec4(0.3, 0.9, 0.2, 1.0),
	vec4(1.0, 0.3, 0.2, 1.0)
);

vec3 hsv2rgb(float hue, float saturation, float value) {
	const vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
	const vec3 p = abs(fract(vec3(hue) + K.xyz) * 6.0 - K.www);
	return value * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), saturation);
}

int extractNibble(ivec4 data, int bit) {
	const int p0 = (data.x >> bit) & 1;
	const int p1 = (data.y >> bit) & 1;
	const int p2 = (data.z >> bit) & 1;
	const int p3 = (data.w >> bit) & 1;
	return p0 | (p1<<1) | (p2<<2) | (p3<<3);
}

float sdfCircle(vec2 p, float radius) {
	return length(p) - radius;
}

float sdfBox(vec2 p, vec2 half_size) {
	vec2 edge_dist = abs(p) - half_size;
	float outside_dist = length(max(edge_dist, 0));
	float inside_dist = min(max(edge_dist.x, edge_dist.y), 0);
	return outside_dist + inside_dist;
}

vec4 getPieceColor(bool parity, int piece, int player) {
	const float dist = sdfBox(uv - 0.5, vec2(0.2));

	const vec4 border_color = vec4(vec3(mix(0.9, 0.1, parity)), 1);
	const vec4 fill_color = playerColors[player];// * float(piece != Empty);

	const float inside = smoothstep(0.05, 0.0, dist);
	const float on_border = smoothstep(0.05, 0.0, dist - 0.07);

	return mix(mix(vec4(0), border_color * fill_color.a, on_border), fill_color, inside);
}

vec4 getSelectionColor(bool parity, int piece) {
	const float dist = sdfCircle(uv - 0.5, 0.35);

	const vec4 border_color = vec4(vec3(mix(0.9, 0.1, parity)), 1);
	const vec4 fill_color = id == cursor_id ? vec4(0.2, 0.3, 0.9, 1.0) : selectionColors[int(piece != 0)];

	const float inside = smoothstep(0.05, 0.0, dist);
	const float on_border = smoothstep(0.05, 0.0, dist - 0.07);

	return mix(mix(vec4(0), border_color * fill_color.a, on_border), fill_color, inside);
}

void main() {
	const int x = id & 7, y = (id>>3) & 3, z = id>>5;

	const bool parity = ((x + y) % 2) == 1;
	const int piece_nibble = extractNibble(field[z * 2], id & 31);
	const int player = extractNibble(field[z * 2 + 1], id & 31);

	const bool selected = piece_nibble >= 8 || id == cursor_id;
	const int piece = piece_nibble & 7;

	const vec4 pieceColor = getPieceColor(parity, piece, player);
	const vec4 selectionColor = getSelectionColor(parity, piece);
	out_color = mix(pieceColor, selectionColor, selectionColor.a * float(selected));
}
