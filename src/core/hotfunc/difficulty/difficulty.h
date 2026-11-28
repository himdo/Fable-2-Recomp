// Difficulty settings (docs/feature/difficulty_settings/): what the host side (the
// difficulty cards in src/core/difficulty_dialog.h, the pad hook and the world-update
// hook) shares with the overrides in Difficulty.cpp.
//
// Threads: the dialog draws on the UI thread and only reads the level and raises
// requests here; the requests are carried out on the game's own threads (the New Game
// replay from the pad read, the level change from the world update).
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

#include <rex/ppc/context.h>

namespace fable2::difficulty {

// The four levels, in card order. Game level = index + 1 (type-33 +80).
struct LevelInfo {
  const char* name;
  const char* subtitle;
  double gold;          // gold gained
  double prices;        // shop items and property: buy x, sell /
  double enemy_damage;  // damage the hero takes
  double enemy_health;  // hits on the hero's enemies take health / this
  double xp;            // XP the hero collects
};
constexpr int kLevelCount = 4;
const LevelInfo& Level(int index);  // 0 Easy .. 3 Hardcore

// Player 1's level as a card index, or -1 when there is no hero (title screen,
// loading). Reads guest memory; safe from any thread (word-sized reads).
int CurrentIndex();

// --- The cards ---------------------------------------------------------------------------

enum class DialogMode : int { kClosed = 0, kNewGame = 1, kInGame = 2 };

// The dialog's state, shared between the UI thread and the guest threads.
struct DialogState {
  std::atomic<int> mode{0};        // DialogMode
  std::atomic<int> nav_x{0};       // left / right presses
  std::atomic<int> activate{0};    // A / Enter
  std::atomic<int> back{0};        // B / Esc
  std::atomic<int> toggle{0};      // Y: the Hardcore card's saving switch
  // The Hardcore card's saving switch (New Game): on = save on quit only (game level 5).
  // Starts as [patches] hardcore_saving says each time the New Game cards open.
  std::atomic<bool> lock_saving{false};
  std::atomic<bool> suppress_pad{false};  // keep blanking the pad until it is released
  std::atomic<int> chosen{-1};     // set by the dialog on confirm (card index)
  std::atomic<int> chosen_mode{0}; // the DialogMode the choice was made in
};
DialogState& Dialog();

inline bool DialogOpen() { return Dialog().mode.load() != int(DialogMode::kClosed); }

// The in-game key (F8; F7 is the SDK achievements overlay): opens the cards over a running game. A debug tool:
// every level can be set there, Hardcore and Hardcore+ too (the Options > Game row keeps decision 2).
void RequestInGameDialog();

// Called by the dialog on the UI thread when a card is confirmed.
void Choose(int index);
// Called by the dialog when it is closed without a choice (in game only).
void CloseDialog();
// Called by the dialog on B on the New Game cards: closes them and, on the next pad read
// on the card thread, puts the game's boy and girl cards back (FE_20 plays again).
void BackToGenderCards();

// True while the game's own boy and girl cards take input (front-end state +24 is 2, 3 or
// 4, no hero yet and our cards are closed): the dialog then draws A / B prompts for them,
// which the game's screen doesn't have. Reads guest memory; safe from the UI thread.
bool GenderCardsWaitingForInput();

// True while the New Game cards are held for a girl (GIRL_SELECTED), for the Easy card's
// picture.
bool NewGameIsGirl();

// Called by the pad read (fable2_photo_mode.h on_pad_read) for user 0 on the
// game's thread, after the game's read: feeds the dialog, runs the New Game replay
// when a card was chosen, and returns true when the game must see an empty pad.
bool OnPadRead(PPCContext& ctx, uint8_t* base, uint16_t buttons, int16_t lx);

// Called on every world update (the world-thread hook in fable_2_app.h): applies a
// level that was chosen on New Game or in game, once the hero exists.
void OnWorldUpdate(PPCContext& ctx, uint8_t* base);

// --- Hardcore saving: save on quit only (technical.md 23) ---------------------------------
//
// A Hardcore hero started with the card's saving switch on is stored at game level 5. For
// it the pause Save box has only Save and Quit, the leave-area reload box never shows, and
// closing the window does a Save and Quit first.

// True when player 1 is such a hero. Reads guest memory; safe from any thread.
bool SavingLocked();

// The app's way to close the window (UI thread), set once at startup.
void SetCloseWindow(std::function<void()> close);
// UI thread, from the app's OnWindowCloseRequested: false holds the close while the game
// saves and quits (a second close after 15 s closes anyway).
bool OnWindowCloseRequested();
// True while a held close is saving and quitting (for the dialog's notice).
bool ClosePending();
// UI thread, every frame: closes the window once the save is written and the front end is up.
void PollClose();

// --- Options > Game: the Difficulty row (src/core/hotfunc/difficulty/OptionsDifficultyRow.cpp) ---

// Player 1's level as shown on a slot label: "Easy" .. "Hardcore", "Hardcore+" with saving
// locked; "" when there is no hero.
const char* CurrentLevelName();
// Sets player 1's level to card index 0..2 right away, on this guest thread (the pause
// menu stops the world update, so the F8 path's apply would wait until the game resumes).
// Refused on a Hardcore hero (decision 2). True when the level changed.
bool ApplyLevelNow(PPCContext& ctx, uint8_t* base, int index);

// --- The level of each save slot, for a slot label ("Hero 1 (Medium)", not done yet) ----
//
// The level of each save slot is kept in <slot folder>/recomp_difficulty.txt (a file
// inside HeroNNN/ survives saves: technical.md 13.8a). It is written after each save made
// in this run, and read with the slot names from Headers/00000001/HeroNNN.header.

// The SDK's user data folder (<exe>/saves), from OnConfigurePaths.
void SetSaveRoot(const std::filesystem::path& root);
// UI thread, every frame (throttled inside): writes the level file next to a save that
// was just written, and refreshes the slot-name -> level table. No guest memory.
void PollSaveSlots();

}  // namespace fable2::difficulty
