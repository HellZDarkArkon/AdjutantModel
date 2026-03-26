#include "Engine/Framework.h"

int main()
{
	Framework fw;

	if (!fw.Init())
		return -1;

	fw.Run();   // blocks until window is closed
	return 0;
	// fw.~Framework() calls Shutdown() automatically
}