#pragma once

#include <atomic>

void processModuleCommandQueue(std::atomic<bool>* const bSignalEnd);
