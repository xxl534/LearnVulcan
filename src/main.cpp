#include <vk_engine.h>

int main(int argc, char* argv[])
{
	VulkanEngine engine;

	engine.Init();	
	
	engine.run();	

	engine.Cleanup();	

	return 0;
}
