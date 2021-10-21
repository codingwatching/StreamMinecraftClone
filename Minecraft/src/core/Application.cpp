#include "core/Application.h"
#include "core.h"
#include "core/Window.h"
#include "core/Ecs.h"
#include "core/Scene.h"
#include "core/AppData.h"
#include "renderer/Renderer.h"
#include "renderer/Font.h"
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

				Scene::update(deltaTime);

				window.swapBuffers();
				window.pollInput();
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
			static Window* window = Window::create(Settings::Window::width, Settings::Window::height, Settings::Window::title);
			return *window;
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