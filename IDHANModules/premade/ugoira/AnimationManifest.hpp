#pragma once

#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <vector>

#include "ModuleCommon.hpp"

//! What Hydrus falls back to when a frame carries no delay (UGOIRA_DEFAULT_FRAME_DURATION_MS).
constexpr int UGOIRA_DEFAULT_FRAME_DELAY_MS { 125 };

//! One entry of a Ugoira's animation.json.
struct UgoiraFrame
{
	std::string m_file {}; //!< The member path of this frame inside the zip.
	int m_delay_ms { UGOIRA_DEFAULT_FRAME_DELAY_MS }; //!< How long the frame is shown, in milliseconds.
};

//! Parses a Ugoira animation.json. Accepts a bare array of frames or an object carrying a "frames"
//! array; both are in circulation.
[[nodiscard]] std::expected< std::vector< UgoiraFrame >, idhan::ModuleError > parseAnimationManifest(
	std::span< const std::byte > bytes );
