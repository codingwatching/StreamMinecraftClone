#include "core.h"
#include "world/World.h"
#include "renderer/Shader.h"
#include "renderer/Texture.h"
#include "renderer/Camera.h"
#include "utils/TexturePacker.h"
#include "core/Input.h"
#include "core/Application.h"
#include "world/BlockMap.h"
#include "gameplay/PlayerController.h"
#include "core/File.h"

#include <random>

namespace Minecraft
{
	namespace World
	{
		// Internal declarations
		static void loadWorldTexture();
		static void checkChunkRadius();
		static void synchronizeChunks();
		static void chunkWorker();

		// Members
		static RenderState renderState;
		static Camera camera;
		static PlayerController playerController;
		static Shader shader;
		static Texture worldTexture;
		static Chunk* chunks;
		static std::unordered_set<glm::ivec2> loadedChunkPositions;
		static const uint16 chunkRadius = 8;
		// Area of circle is PI * r^2, we'll round PI up to 4
		static const uint16 chunkCapacity = (chunkRadius + 1) * (chunkRadius + 1) * 4;
		static const std::string worldSavePath = "world";
		static std::thread workerThreads[16];

		static std::mutex mutex;
		static std::condition_variable cvar;
		static bool chunkNeedsWork = false;
		static bool threadRunning = true;


		static int32 seed = 0;

		void init()
		{
			srand((unsigned long)time(NULL));
			seed = (int32)(((float)rand() / (float)RAND_MAX) * 1000.0f);
			g_logger_info("World seed: %d", seed);

			// Initialize blocks
			//TexturePacker::packTextures("C:/dev/C++/MinecraftClone/assets/images/block", "textureFormat.yaml");
			chunks = (Chunk*)g_memory_allocate(sizeof(Chunk) * chunkCapacity);
			g_memory_zeroMem(chunks, sizeof(Chunk) * chunkCapacity);

			BlockMap::loadBlocks("textureFormat.yaml", "blockFormats.yaml");
			BlockMap::uploadTextureCoordinateMapToGpu();
			shader = NShader::createShader("C:/dev/C++/MinecraftClone/assets/shaders/default.glsl");
			loadWorldTexture();


			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_BUFFER, BlockMap::getTextureCoordinatesTextureId());
			NShader::uploadInt(shader, "uTexCoordTexture", 1);

			// Create a chunk
			File::createDirIfNotExists(worldSavePath.c_str());
			Chunk::info();

			// Setup camera
			camera.position = glm::vec3(0, 257.0f, 1.0f);
			camera.fov = 45.0f;
			camera.orientation = glm::vec3(0.0f, 0.0f, 0.0f);
			playerController.init(&camera);

			checkChunkRadius();

			workerThreads[0] = std::thread(chunkWorker);
		}

		void update(float dt)
		{
			glm::mat4 projection = camera.calculateProjectionMatrix();
			glm::mat4 view = camera.calculateViewMatrix();
			NShader::uploadMat4(shader, "uProjection", projection);
			NShader::uploadMat4(shader, "uView", view);
			NShader::uploadVec3(shader, "uSunPosition", glm::vec3{ 1, 355, 1 });

			playerController.update(dt);

			if (Input::isKeyPressed(GLFW_KEY_F1))
			{
				Application::lockCursor(false);
			}
			else if (Input::isKeyPressed(GLFW_KEY_F2))
			{
				Application::lockCursor(true);
			}

			const glm::vec3& playerPosition = playerController.playerCamera->position;
			glm::ivec2 playerPositionInChunkCoords = glm::ivec2{
				playerPosition.x / 16,
				playerPosition.z / 16
			};
			for (int i = 0; i < chunkCapacity; i++)
			{
				Chunk& chunk = chunks[i];
				if (chunk.loaded && !chunk.working)
				{
					NShader::uploadIVec2(shader, "uChunkPos", chunk.chunkCoordinates);
					chunk.render();

					const glm::ivec2 localChunkPos = chunk.chunkCoordinates - playerPositionInChunkCoords;
					if (!chunk.shouldUnload && (localChunkPos.x * localChunkPos.x) + (localChunkPos.y * localChunkPos.y) >= chunkRadius * chunkRadius)
					{
						chunk.unload();
						loadedChunkPositions.erase(chunk.chunkCoordinates);
						{
							std::lock_guard lock(mutex);
							chunkNeedsWork = true;
						}
						cvar.notify_all();
					}
				}
			}

			synchronizeChunks();
			checkChunkRadius();
		}

		void cleanup()
		{
			{
				std::lock_guard<std::mutex> lock(mutex);
				threadRunning = false;
				chunkNeedsWork = true;
			}
			cvar.notify_all();
			workerThreads[0].join();

			for (int i = 0; i < chunkCapacity; i++)
			{
				Chunk& chunk = chunks[i];
				chunk.freeCpu();
				chunk.freeGpu();
			}

			g_memory_free(chunks);
		}

		static Chunk* findUnloadedChunk()
		{
			for (int i = 0; i < chunkCapacity; i++)
			{
				Chunk& chunk = chunks[i];
				if (!chunk.loaded && !chunk.working)
				{
					return &chunk;
				}
			}

			g_logger_warning("Ran out of room of loaded chunks. Number of chunks allowed %d", chunkCapacity);
			return nullptr;
		}

		static void checkChunkRadius()
		{
			const glm::vec3 playerPosition = playerController.playerCamera->position;
			glm::ivec2 position = glm::ivec2{
				playerPosition.x / 16,
				playerPosition.z / 16
			};
			glm::ivec2 playerPosChunkCoords = position;

			bool needsWork = false;
			// 0 RIGHT, 1 UP, 2 LEFT, 3 DOWN
			uint8 direction = 0;
			uint8 distance = 1;
			uint8 timeToChangeDistance = 0;
			uint8 travelled = 0;
			while (true)
			{
				glm::ivec2 localPos = playerPosChunkCoords - position;
				if ((localPos.x * localPos.x) + (localPos.y * localPos.y) < chunkRadius * chunkRadius)
				{
					auto iter = loadedChunkPositions.find(position);
					if (iter == loadedChunkPositions.end())
					{
						Chunk* chunk = findUnloadedChunk();
						if (chunk)
						{
							chunk->chunkCoordinates.x = position.x;
							chunk->chunkCoordinates.y = position.y;
							chunk->load();
							loadedChunkPositions.insert(position);
							needsWork = true;
						}
					}
				}

				switch (direction)
				{
				case 0:
					position.x++;
					break;
				case 1:
					position.y++;
					break;
				case 2:
					position.x--;
					break;
				case 3:
					position.y--;
					break;
				}

				travelled++;
				if (travelled >= distance)
				{
					direction = (direction + 1) % 4;
					travelled = 0;

					timeToChangeDistance++;
					if (timeToChangeDistance > 1)
					{
						timeToChangeDistance = 0;
						distance++;
					}

					if (distance > chunkRadius * 2)
					{
						break;
					}
				}
			}

			if (needsWork)
			{
				{
					std::lock_guard lock(mutex);
					chunkNeedsWork = true;
				}
				cvar.notify_all();
			}
		}

		static void synchronizeChunks()
		{
			for (int i = 0; i < chunkCapacity; i++)
			{
				Chunk& chunk = chunks[i];
				if (chunk.shouldUnload && !chunk.working)
				{
					g_logger_info("Unloading from GPU chunk<%d, %d>", chunk.chunkCoordinates.x, chunk.chunkCoordinates.y);
					chunk.freeGpu();
				}

				if (chunk.shouldLoad && !chunk.working)
				{
					g_logger_info("Uploading to GPU chunk<%d, %d>", chunk.chunkCoordinates.x, chunk.chunkCoordinates.y);
					chunk.uploadToGPU();
				}
			}
		}

		static void loadWorldTexture()
		{
			worldTexture = TextureBuilder()
				.setFormat(ByteFormat::RGBA8_UI)
				.setMagFilter(FilterMode::Nearest)
				.setMinFilter(FilterMode::Nearest)
				.setFilepath("test.png")
				.generate(true);

			// Upload the world texture
			glActiveTexture(GL_TEXTURE0);
			worldTexture.bind();
			NShader::uploadInt(shader, "uTexture", 0);

			glUseProgram(shader.programId);
		}

		static void chunkWorker()
		{
			while (threadRunning)
			{
				{
					std::unique_lock lock(mutex);
					cvar.wait(lock, [&] { return chunkNeedsWork; });
				}

				for (int i = 0; i < chunkCapacity; i++)
				{
					Chunk& chunk = chunks[i];
					if (chunk.shouldLoad && chunk.working)
					{
						g_logger_info("Loading on CPU chunk<%d, %d>", chunk.chunkCoordinates.x, chunk.chunkCoordinates.y);
						if (Chunk::exists(worldSavePath, chunk.chunkCoordinates.x, chunk.chunkCoordinates.y))
						{
							chunk.deserialize(worldSavePath, chunk.chunkCoordinates.x, chunk.chunkCoordinates.y);
						}
						else
						{
							chunk.generate(chunk.chunkCoordinates.x, chunk.chunkCoordinates.y, seed);
						}
						chunk.generateRenderData();

						{
							std::lock_guard<std::mutex> lock(mutex);
							chunk.working = false;
						}
					}
					else if (chunk.shouldUnload && chunk.working)
					{
						g_logger_info("Unloading on CPU chunk<%d, %d>", chunk.chunkCoordinates.x, chunk.chunkCoordinates.y);
						chunk.freeCpu();

						{
							std::lock_guard<std::mutex> lock(mutex);
							chunk.working = false;
						}
					}
				}

				{
					std::lock_guard<std::mutex> lock(mutex);
					chunkNeedsWork = false;
				}
			}
		}
	}
}