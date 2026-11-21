#pragma once

namespace mango::cli
{

// Linux's main: one Mango per user runs the engine; the others attach to it.
//   mango            attaches to the running Mango (see ControlClient.hpp),
//                    or runs the engine with the prompt if none runs
//   mango --service  runs the engine with no prompt (the systemd unit)
// Whoever runs the engine holds the InstanceLock and opens the control
// socket (see ControlServer.hpp).
int RunLinux(int argc, char** argv);

}  // namespace mango::cli
