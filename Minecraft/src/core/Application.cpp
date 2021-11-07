#include "core/Application.h"
#include "core.h"
#include "core/Window.h"
#include "core/Ecs.h"
#include "core/Scene.h"
#include "core/AppData.h"
#include "renderer/Renderer.h"
#include "renderer/Font.h"
#include "renderer/Framebuffer.h"
#include "renderer/Shader.h"
#include "physics/Physics.h"
#include "input/Input.h"
#include "input/KeyBindings.h"
#include "utils/Settings.h"
#include "gui/Gui.h"
#include "gui/GuiElements.h"

namespace Minecraft
{
	namespace Application
	{
		static Ecs::Registry& getRegistry();
		static void freeWindow();
		static void freeRegistry();

		// Internal variables
		static Framebuffer mainFramebuffer;
		static bool dumpScreenshot = false;
		static bool screenshotMustBeSquare = false;
		static std::string screenshotName = "";

		static Shader mainFramebufferShader;
		static Shader compositeShader;
		static uint32 mainRectVao;
		static float mainRectVerts[24] = {
			1.0f, 1.0f, 1.0f, 1.0f,     // Top-right pos and uvs
			-1.0f, 1.0f, 0.0f, 1.0f,   // Top-left pos and uvs
			1.0f, -1.0f, 1.0f, 0.0f,   // Bottom-right pos and uvs

			1.0f, -1.0f, 1.0f, 0.0f,   // Bottom-right pos and uvs
			-1.0f, 1.0f, 0.0f, 1.0f,   // Top-left pos and uvs
			-1.0f, -1.0f, 0.0f, 0.0f  // Bottom-left pos and uvs
		};

		void init()
		{
			// Initialize GLFW/Glad
			Window::init();
			Window& window = getWindow();
			if (!window.windowPtr)
			{
				g_logger_error("Error: Could not create window.");
				return;
			}

			// Initialize all other subsystems
			AppData::init();
			Ecs::Registry& registry = getRegistry();
			Renderer::init(registry);
			Fonts::init();
			Physics::init();
			Scene::init(SceneType::MainMenu, registry);
			KeyBindings::init();
			Gui::init();
			GuiElements::init();

			// Initialize the framebuffers
			Texture opaqueTextureSpec;
			opaqueTextureSpec.type = TextureType::_2D;
			opaqueTextureSpec.width = window.width;
			opaqueTextureSpec.height = window.height;
			opaqueTextureSpec.magFilter = FilterMode::Linear;
			opaqueTextureSpec.minFilter = FilterMode::Linear;
			opaqueTextureSpec.wrapS = WrapMode::None;
			opaqueTextureSpec.wrapT = WrapMode::None;
			opaqueTextureSpec.format = ByteFormat::RGBA_16F;

			Texture accumulationTextureSpec;
			accumulationTextureSpec.type = TextureType::_2D;
			accumulationTextureSpec.width = window.width;
			accumulationTextureSpec.height = window.height;
			accumulationTextureSpec.magFilter = FilterMode::Linear;
			accumulationTextureSpec.minFilter = FilterMode::Linear;
			accumulationTextureSpec.wrapS = WrapMode::None;
			accumulationTextureSpec.wrapT = WrapMode::None;
			accumulationTextureSpec.format = ByteFormat::RGBA_16F;

			Texture revealTextureSpec;
			revealTextureSpec.type = TextureType::_2D;
			revealTextureSpec.width = window.width;
			revealTextureSpec.height = window.height;
			revealTextureSpec.magFilter = FilterMode::Linear;
			revealTextureSpec.minFilter = FilterMode::Linear;
			revealTextureSpec.wrapS = WrapMode::None;
			revealTextureSpec.wrapT = WrapMode::None;
			revealTextureSpec.format = ByteFormat::R8_F;

			mainFramebuffer = FramebufferBuilder(window.width, window.height)
				.addColorAttachment(opaqueTextureSpec)
				.addColorAttachment(accumulationTextureSpec)
				.addColorAttachment(revealTextureSpec)
				.includeDepthStencilBuffer()
				.generate();
			mainFramebuffer.bind();
			glViewport(0, 0, window.width, window.height);
			mainFramebuffer.unbind();

			// Initialize rendering state for blitting the main framebuffer to the screen
			mainFramebufferShader.compile("assets/shaders/MainFramebuffer.glsl");
			compositeShader.compile("assets/shaders/CompositeShader.glsl");

			glGenVertexArrays(1, &mainRectVao);
			glBindVertexArray(mainRectVao);

			uint32 vbo;
			glGenBuffers(1, &vbo);
			glBindBuffer(GL_ARRAY_BUFFER, vbo);

			glBufferData(GL_ARRAY_BUFFER, sizeof(mainRectVerts), mainRectVerts, GL_STATIC_DRAW);

			glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, (void*)0);
			glEnableVertexAttribArray(0);

			glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, (void*)(sizeof(float) * 2));
			glEnableVertexAttribArray(1);
		}

		void run()
		{
			// Run game loop
			// Start with a 60 fps frame rate
			Window& window = getWindow();
			float previousTime = (float)glfwGetTime() - 0.016f;
			bool inMainMenu = true;
			const float targetFps = 0.016f;
			const float nextTarget = 0.032f;
			while (!window.shouldClose())
			{
				float deltaTime = (float)glfwGetTime() - previousTime;
				previousTime = (float)glfwGetTime();

				//if (Input::keyBeginPress(GLFW_KEY_F8))
				//{
				//	static bool capturing = false;
				//	if (!capturing)
				//	{
				//		capturing = true;
				//		OPTICK_START_CAPTURE();
				//	}
				//	else
				//	{
				//		capturing = false;
				//		OPTICK_STOP_CAPTURE();
				//		OPTICK_SAVE_CAPTURE("c:/tmp/optickCapture.opt");
				//	}
				//}

				// TODO: Do I want to keep this?
				float actualFps = deltaTime;
				if (targetFps - actualFps > 0)
				{
					std::this_thread::sleep_for(std::chrono::milliseconds((int)((targetFps - actualFps) * 1000)));
					deltaTime = targetFps;
				}
				else if (targetFps - nextTarget > 0)
				{
					std::this_thread::sleep_for(std::chrono::milliseconds((int)((nextTarget - actualFps) * 1000)));
					deltaTime = nextTarget;
				}

				if (mainFramebuffer.width != getWindow().width || mainFramebuffer.height != getWindow().height)
				{
					mainFramebuffer.width = getWindow().width;
					mainFramebuffer.height = getWindow().height;
					mainFramebuffer.regenerate();
					mainFramebuffer.bind();
					glViewport(0, 0, getWindow().width, getWindow().height);
				}

				mainFramebuffer.bind();
				const float zeroFillerVec[4] = { 0.0f, 0.0f, 0.0f };
				glClearBufferfv(GL_COLOR, 1, &zeroFillerVec[0]);
				const float oneFillerVec[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
				glClearBufferfv(GL_COLOR, 2, &oneFillerVec[0]);
				Scene::update(deltaTime);

				// Set the render state for compositing our transparent and opaque buffers
				glDepthFunc(GL_ALWAYS);
				glEnable(GL_BLEND);
				glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

				// Bind the opaque framebuffer and composite
				compositeShader.bind();

				// Draw the screen quad
				glActiveTexture(GL_TEXTURE0);
				glBindTexture(GL_TEXTURE_2D, mainFramebuffer.getColorAttachment(1).graphicsId);
				glActiveTexture(GL_TEXTURE1);
				glBindTexture(GL_TEXTURE_2D, mainFramebuffer.getColorAttachment(2).graphicsId);
				compositeShader.uploadInt("accumulationTexture", 0);
				compositeShader.uploadBool("revealTexture", 1);

				glBindVertexArray(mainRectVao);
				glDrawArrays(GL_TRIANGLES, 0, 6);

				// Unbind all framebuffers and render the composited image
				glDisable(GL_DEPTH_TEST);
				glDepthMask(GL_TRUE);
				glDisable(GL_BLEND);

				glBindFramebuffer(GL_FRAMEBUFFER, 0);
				glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
				glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

				mainFramebufferShader.bind();
				glBindTextureUnit(0, mainFramebuffer.getColorAttachment(0).graphicsId);
				mainFramebufferShader.uploadInt("uMainTexture", 0);

				glBindVertexArray(mainRectVao);
				glDrawArrays(GL_TRIANGLES, 0, 6);

				glEnable(GL_DEPTH_TEST);

				window.swapBuffers();
				window.pollInput();

				if (dumpScreenshot)
				{
					if (screenshotName == "")
					{
						time_t rawtime;
						struct tm* timeinfo;
						char buffer[80];

						time(&rawtime);
						timeinfo = localtime(&rawtime);

						strftime(buffer, sizeof(buffer), "%d-%m-%Y %H.%M.%S", timeinfo);
						screenshotName = std::string(buffer);
					}

					uint8* pixels = (uint8*)g_memory_allocate(sizeof(uint8) * mainFramebuffer.width * mainFramebuffer.height * 4);
					int outputWidth = mainFramebuffer.width;
					int outputHeight = mainFramebuffer.height;
					int startX = 0;
					int startY = 0;
					if (screenshotMustBeSquare)
					{
						if (outputWidth > outputHeight)
						{
							outputWidth = outputHeight;
							startX = (outputWidth - outputHeight) / 2;
						}
						else
						{
							outputHeight = outputWidth;
							startY = (outputHeight - outputWidth) / 2;
						}
					}

					glReadPixels(startX, startY, outputWidth, outputHeight, GL_RGBA, GL_UNSIGNED_BYTE, (void*)pixels);
					stbi_flip_vertically_on_write(true);
					std::string filepath = AppData::screenshotsPath + "/" + screenshotName + ".png";
					stbi_write_png(filepath.c_str(), outputWidth, outputHeight, 4, (void*)pixels, sizeof(uint8) * outputWidth * 4);
					g_memory_free(pixels);
					dumpScreenshot = false;
				}
			}
		}

		void free()
		{
			// Free all resources
			GuiElements::free();
			getRegistry().free();
			Window& window = getWindow();
			window.destroy();
			Scene::free();
			Renderer::free();
			Window::free();

			// Free the pointers now that everything should be cleaned up
			freeWindow();
			freeRegistry();
		}

		Window& getWindow()
		{
			static Window* window = Window::create(Settings::Window::title);
			return *window;
		}

		void takeScreenshot(const char* filename, bool mustBeSquare)
		{
			dumpScreenshot = true;
			screenshotName = filename;
			screenshotMustBeSquare = mustBeSquare;
		}

		static Ecs::Registry& getRegistry()
		{
			static Ecs::Registry* registry = new Ecs::Registry();
			return *registry;
		}

		static void freeWindow()
		{
			delete& getWindow();
		}

		static void freeRegistry()
		{
			delete& getRegistry();
		}
	}
}