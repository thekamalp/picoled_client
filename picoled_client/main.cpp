// Main entry point

#include "picoled_client.h"

#ifdef _WIN32
#pragma comment(lib, "Ws2_32.lib")
#endif

int main()
{
	picoled p;
	k3winObj::WindowLoop();

	return 0;
}
