#include "ShutdownCoordinator.hpp"

// The Windows app ends the stream itself when it closes.
void ShutdownCoordinator::InstallSignalHandler() {}
