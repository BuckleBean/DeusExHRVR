#pragma once
#include "SharedPair.h"
namespace GamepadBridge {
void EnableInteractionVisual(bool enabled);
bool InteractionVisualHeld(uint64_t now);
// Interact is down, from either hand, whether or not the finger curl is enabled.
bool InteractionHeld(uint64_t now);
// The most recent Interact press came from the left grip.
bool LastInteractWasLeft();
void SetChannel(Transport::Header* header);
void OnPresent(bool capture);
void MenuTick(unsigned screenReasons);
}
