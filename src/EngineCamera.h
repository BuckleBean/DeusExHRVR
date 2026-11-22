#pragma once
#include <cstdint>
#include "SharedPair.h"
struct ID3D11Device;
namespace EngineCamera {
bool DialogueEnabled();
bool DialogueAvailable();
void SetDialogueEnabled(bool enabled);
// HUD on the hands: the saved HandHud setting ([VR], default 1) and whether the
// hand HUD is running now. SetHandHudConfigured also switches it live.
bool HandHudConfigured();
bool HandHudActive();
void SetHandHudConfigured(bool enabled);
Transport::RenderInfo OnPresent(uint64_t frame, bool capture);
void SetChannel(Transport::Header* header);
bool SnapTurnView(float& yaw);
// Separate from gameplay classification: cover retains gameplay button bindings.
bool NativeCoverControls();
void SetDirectSnapEnabled(bool enabled);
void CancelDirectSnap();
bool RequestDirectSnap(float rightDegrees);
// Local patch: the game's quickbar auto-hide setting (tilde). Returns the value
// found, setting it to 1 first if asked, or -1 if the game's layout doesn't match.
int QuickBarAutoHide(bool set);
// VR menu: 0 unavailable, 1 always shown, 2 auto-hide. SetQuickBar applies live and is remembered.
unsigned QuickBarSetting();
void SetQuickBar(bool autoHide);
void InstallQuickBarHooks();
void InstallTakedownHooks();
bool ImmersiveScopeButton();
// Arms hands calibration. Called from GamepadBridge so the trigger can be a
// bindable [Buttons] target instead of a pose gesture or an INI edit.
void ArmHandCalibration();
// Grenade landing marker (ThrowAim.h). GamepadBridge reports while the throw
// button is held past its hold delay, and how long it was held at release.
bool ThrowMarkerEnabled();
void SetThrowAiming(bool aiming);
void NoteThrowRelease(uint64_t heldMs);
// True while the game offers to detonate a thrown remote device (the condition
// behind its "Weapon_Detonate" prompt). A press of the throw input that starts
// then is passed to the game as a plain held RB, not a throw.
bool DetonateReady();
// True while the game's current interaction target belongs to the left hand.
bool LeftHandOwnsPrompt();
// Something the left hand grabbed is being carried (triggers swap so left throws).
bool LeftHandCarrying();
// Two-handed weapons: the left hand is close enough to take the weapon, and the grip
// press that is holding it there.
bool LeftHandNearWeapon();
void SetLeftHandOnWeapon(bool on);
// LB as actually sent to the game after the button layout (sprint).
void SetGameSprintHeld(bool on);
void NoteDetonatePress(uint64_t heldMs);
void Recenter();
// Local patch: bit mask of active virtual-screen reasons (1 interaction, 2 menu, 4 video, 8 game over, 16 scope,
// 32 in-game menu, 64 e-reader/news reader: buttons only, the display stays tracked;
// snap turn still works over a reader. 128 a wheel screen pausing gameplay: keep
// [Buttons], and the display is left as it is).
unsigned CurrentScreenReasons();
unsigned MovementDirection();
unsigned MovementResponse();
unsigned InteractionAim();
float ConfiguredWorldScale();
float ActiveWorldScale();
void SetConfiguredWorldScale(float value);
// Immersive scope magnification (ScopeMagnification, 1-12); applies on the next frame.
float ScopeMagnification();
bool SetScopeMagnification(float value);
// VR settings menu tabs (transport 24). Each returns 0 when the feature could
// not start (its hook was refused); otherwise ThrowAimSetting is the
// DirectionConfig::Source + 1 and the others 1 off / 2 on. The setters apply
// from the next frame; an unavailable feature ignores them.
unsigned ThrowAimSetting();
void SetThrowAim(unsigned source);
unsigned ThrowMarkerSetting();
void SetThrowMarker(bool enabled);
unsigned CoverFollowSetting();
void SetCoverFollow(bool enabled);
// VR crosshair: CoverCrosshair::Off, First, Third or Both. Applies at once.
unsigned CrosshairSetting();
void SetCrosshair(unsigned mode);
unsigned ImmersiveScopeSetting();
void SetImmersiveScope(bool enabled);
void SetInteractionAim(unsigned value);
void SetMovementResponse(unsigned value);
void SetMovementDirection(unsigned value);
// Luma port: forward the engine's D3D11 device to ShaderSwap so it can build
// replacement shaders and issue PSSetShader overrides. Called from
// NativeTransport::Producer::Init once the device is first available.
void SetShaderSwapDevice(ID3D11Device* device);
// Toggle live shader substitution (F12). Returns the new state.
bool ToggleShaderSubstitution();
}
