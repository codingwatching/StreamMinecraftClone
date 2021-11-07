#type vertex
#version 430 core
layout (location = 0) in uint aData1;
layout (location = 1) in uint aData2;

layout (location = 10) in ivec2 aChunkPos;
layout (location = 11) in int aBiome;

out vec2 fTexCoords;
flat out uint fFace;
out vec3 fFragPosition;
out vec3 fColor;
out float fLightLevel;
out vec3 fLightColor;

uniform samplerBuffer uTexCoordTexture;
uniform mat4 uProjection;
uniform mat4 uView;

#define POSITION_INDEX_BITMASK uint(0x1FFFF)
#define FACE_BITMASK uint(0xE0000000)
#define TEX_ID_BITMASK uint(0x1FFE0000)
#define UV_INDEX_BITMASK uint(0x3)
#define COLOR_BLOCK_BIOME_BITMASK uint(0x4)
#define LIGHT_LEVEL_BITMASK uint(0xF8)
#define LIGHT_COLOR_BITMASK_R uint(0x00700)
#define LIGHT_COLOR_BITMASK_G uint(0x03800)
#define LIGHT_COLOR_BITMASK_B uint(0x1C000)

#define BASE_17_WIDTH uint(17)
#define BASE_17_DEPTH uint(17)
#define BASE_17_HEIGHT uint(289)

void extractPosition(in uint data, out vec3 position)
{
	uint positionIndex = data & POSITION_INDEX_BITMASK;
	uint z = positionIndex % BASE_17_WIDTH;
	uint x = (positionIndex % BASE_17_HEIGHT) / BASE_17_DEPTH;
	uint y = (positionIndex - (x * BASE_17_DEPTH) - z) / BASE_17_HEIGHT;
	position = vec3(float(x), float(y), float(z));
}

void extractFace(in uint data, out uint face)
{
	face = ((data & FACE_BITMASK) >> 29);
}

void extractTexCoords(in uint data1, in uint data2, out vec2 texCoords)
{
	uint textureId = ((data1 & TEX_ID_BITMASK) >> 17);
	uint uvIndex = data2 & UV_INDEX_BITMASK;
	int index = int((textureId * uint(8)) + (uvIndex * uint(2)));
	texCoords.x = texelFetch(uTexCoordTexture, index + 0).r;
	texCoords.y = texelFetch(uTexCoordTexture, index + 1).r;
}

void extractColorVertexBiome(in uint data2, out bool colorVertexBiome)
{
	colorVertexBiome = bool(data2 & COLOR_BLOCK_BIOME_BITMASK);
}

void extractLightLevel(in uint data2, out float lightLevel)
{
	lightLevel = float((data2 & LIGHT_LEVEL_BITMASK) >> 3);
}

void extractLightColor(in uint data2, out vec3 lightColor)
{
	lightColor.r = float((data2 & LIGHT_COLOR_BITMASK_R) >> 8) / 8.0;
	lightColor.g = float((data2 & LIGHT_COLOR_BITMASK_G) >> 11) / 8.0;
	lightColor.b = float((data2 & LIGHT_COLOR_BITMASK_B) >> 14) / 8.0;
}

void main()
{
	extractPosition(aData1, fFragPosition);
	extractFace(aData1, fFace);
	extractTexCoords(aData1, aData2, fTexCoords);
	bool colorVertexByBiome;
	extractColorVertexBiome(aData2, colorVertexByBiome);
	extractLightLevel(aData2, fLightLevel);
	extractLightColor(aData2, fLightColor);

	// Convert from local Chunk Coords to world Coords
	fFragPosition.x += float(aChunkPos.x) * 16.0;
	fFragPosition.z += float(aChunkPos.y) * 16.0;

	fColor = vec3(1, 1, 1);
	if (colorVertexByBiome) 
	{
		fColor = vec3(109.0 / 255.0, 184.0 / 255.0, 79.0 / 255.0);
	}

	gl_Position = uProjection * uView * vec4(fFragPosition, 1.0);
}

#type fragment
#version 430 core
layout (location = 1) out vec4 accumulation;
layout (location = 2) out float reveal;

in vec2 fTexCoords;
flat in uint fFace;
in vec3 fFragPosition;
in vec3 fColor;
in float fLightLevel;
in vec3 fLightColor;

uniform sampler2D uTexture;
uniform vec3 uSunDirection;
uniform vec3 uPlayerPosition;
uniform int uChunkRadius;
uniform bool uIsDay;

vec3 lightColor = vec3(0.8, 0.8, 0.8);

void faceToNormal(in uint face, out vec3 normal)
{
	switch(face)
	{
		case uint(0):
			normal = vec3(0, 0, -1);
			break;
		case uint(1):
			normal = vec3(0, 0, 1);
			break;
		case uint(2):
			normal = vec3(0, -1, 0);
			break;
		case uint(3):
			normal = vec3(0, 1, 0);
			break;
		case uint(4):
			normal = vec3(-1, 0, 0);
			break;
		case uint(5):
			normal = vec3(1, 0, 0);
			break;
	}
}

void main()
{
	// Calculate ambient light
    float ambientStrength = 0.4;
    vec3 ambient = ambientStrength * lightColor;

	// Turn that into diffuse lighting
	vec3 lightDir = normalize(uSunDirection);
	lightColor.b = uIsDay ? 0.8 : 0.9;

	vec3 normal;
	faceToNormal(fFace, normal);
	float diff = max(dot(normal, lightDir), 0.0);
	vec3 diffuse = diff * lightColor;

	float distanceToPlayer = length(fFragPosition - uPlayerPosition);
	float d = (distanceToPlayer / (float(uChunkRadius) * 16.0)) - 0.5;
	d = clamp(d, 0, 1);
	vec4 fogColor = vec4(153.0 / 255.0, 204.0 / 255.0, 1.0, 1.0);

	vec4 objectColor = texture(uTexture, fTexCoords);
	vec3 result = (diffuse + ambient) * objectColor.rgb;

	float baseLightColor = .2;
	float colorLightValue = pow(float(fLightLevel) / 32.0, 1.4) + baseLightColor;
	vec4 lightColor = vec4(colorLightValue, colorLightValue, colorLightValue, 1.0) * vec4(fLightColor, 1.0);

	vec4 fragColor = ((lightColor * vec4(fColor, 1.0)) + vec4(diffuse, 0.0)) * objectColor;
	
	// Weight function
	float weight = clamp(pow(min(1.0, fragColor.a * 10.0) + 0.01, 3.0) * 1e8 * pow(1.0 - gl_FragCoord.z * 0.9, 3.0), 1e-2, 3e3);
	
	// Store pixel color accumulation
	accumulation = vec4(fragColor.rgb * fragColor.a, fragColor.a) * weight;

	// Store pixel revealage threshold
	reveal = fragColor.a;
}