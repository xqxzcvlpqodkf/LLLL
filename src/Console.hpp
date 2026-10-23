#pragma once

#include <Geode/Geode.hpp>

#include "core/Learner.hpp"

#include <string>

// The debug window.
//
// On Windows this opens a real console window next to the game, titled
// "LearnBot". On every platform the same lines also go to
// <mod save dir>/learnbot.log, so nothing is Windows-only.
//
// Everything printed here is written for a person, not for a programmer: no
// stack traces, no tick counts on their own, no jargon without a translation.
namespace dbg {

enum class Tone { Info, Good, Warn, Bad };

// Opens the console (Windows) and starts a fresh log file.
void open();

// One plain-English line.
void say(std::string const& text, Tone tone = Tone::Info);

// Technical extra, only shown when "Debug detail" is enabled.
void detail(std::string const& text);

// A heading, to break a long session into readable chunks.
void section(std::string const& title);

// The first thing printed: what this mod is, in terms anyone can follow.
void banner();

// Turns one learner event into a sentence a player can act on.
void report(learnbot::Event const& event);

// "1.25 s" from a tick count, for the technical lines.
std::string seconds(long long ticks);

// "25.0 million" from a tick count, for budgets.
std::string millions(long long ticks);

} // namespace dbg
