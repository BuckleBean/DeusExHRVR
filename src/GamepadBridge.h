#pragma once
#include "SharedPair.h"
namespace GamepadBridge {
void EnableInteractionVisual(bool enabled);
bool InteractionVisualHeld(uint64_t now);
void SetChannel(Transport::Header* header);
void OnPresent(bool capture);
void MenuTick(unsigned screenReasons);
}
