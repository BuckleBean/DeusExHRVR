#pragma once
// Loaded once at startup, before diagnostic consumers/worker are started.
// These flags gate observation only; never gate rendering or validation.
namespace DiagnosticOptions {
inline bool hands{},runtime{},liveHandConfig{};
}
