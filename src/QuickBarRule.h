#pragma once
// What the quickbar should do. The game forces auto-hide on a controller (input byte 0).
// A choice saved in the VR menu overrides that; with no choice the game's rule stands.
namespace QuickBarRule {
inline constexpr int NoChoice=-1;
inline constexpr bool AutoHide(int choice,bool gameFlag,unsigned char inputByte) {
    return choice>=0?choice!=0:(inputByte==0||gameFlag);
}
inline constexpr unsigned MenuValue(int choice,bool gameFlag,unsigned char inputByte) {
    return AutoHide(choice,gameFlag,inputByte)?2u:1u;
}
}
