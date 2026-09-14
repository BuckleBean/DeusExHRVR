#pragma once
#include "SharedPair.h"
namespace GamepadBridge {
void SetChannel(Transport::Header* header);
void OnPresent(bool capture);
}
