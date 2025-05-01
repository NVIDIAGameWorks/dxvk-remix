#include <iostream>
#include <vector>
#include <thread>

#include "config/config.h"
#include "config/global_options.h"
#include "util_commands.h"
#include "util_circularqueue.h"
#include "util_atomiccircularqueue.h"


using namespace std;
using namespace Commands;
using namespace bridge_util;

const int QUEUE_SIZE = 5;
const int MEM_SIZE = 640;
void* gMemoryData = NULL;

class CommandQueueHistoryTest {
public:
  static void run() {
    cout << "Begin CommandHistoryQueue smoke test" << endl;
    test_smoke();
    cout << "CommandHistoryQueue successfully smoke tested" << endl;
  }

private:
  static void test_smoke() {
    gMemoryData = new char[MEM_SIZE];
    AtomicCircularQueue<Header, Accessor::Writer> commandQueueObject("Client2ServerCommand",
                                gMemoryData,
                                MEM_SIZE,
                                QUEUE_SIZE);

    // Pushing list of commands into the queue
    if (commandQueueObject.push({ Bridge_Syn, 0, 0, 0 }) != Result::Success) {
      throw string("Issue sending command to the queue");
    }
    if (commandQueueObject.push({ Bridge_Ack, 0, 0, 0 }) != Result::Success) {
      throw string("Issue sending command to the queue");
    }
    if (commandQueueObject.push({ IDirect3DDevice9Ex_GetDeviceCaps, 0, 0, 0 }) != Result::Success) {
      throw string("Issue sending command to the queue");
    }

    // Pulling commands from the queue
    Result result = Result::Failure;
    Header pullResult = commandQueueObject.pull(result);
    if (result != Result::Success) {
      throw string("Issue retrieving command from the queue");
    }
    if (pullResult.command != Bridge_Syn) {
      throw string("Retrieved command from the queue is not as expected");
    }

    // Check if Command sent to the queue is consistent
    vector<D3D9Command> commandSent;
    commandSent = commandQueueObject.getWriterQueueData(3);
    if (commandSent[0] != IDirect3DDevice9Ex_GetDeviceCaps || commandSent[1] != Bridge_Ack || commandSent[2] != Bridge_Syn) {
      throw string("Commands sent do not match");
    }

    // Check if Command recieved from the queue is consistent
    vector<D3D9Command> commandReceived;
    commandReceived = commandQueueObject.getReaderQueueData(1);
    if (commandReceived[0] != Bridge_Syn) {
      throw string("Commands received do not match");
    }
  }
};

int main() {
  try {
    CommandQueueHistoryTest::run();
  }
  catch (const string& errorMessage) {
    cerr << errorMessage << endl;
	return -1;
  }
  delete[] gMemoryData;
  return 0;
}

