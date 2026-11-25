# AI-assisted function naming: overview

**Status:** no commit yet (research only) · **Commits:** none · **Last checked against:** 7863041

## What this is

The original game is made of tens of thousands of small pieces of code, and most still have no meaningful name. Giving them real names is what makes the game readable and moddable. AI tools can suggest names quickly, and this project uses them. This page looks at what others have learned about that, and at what has happened in this project. The details and sources are in [technical.md](technical.md).

## What others found

- AI tools that read game code are now common.
- A 2026 study found that, left on their own, they start well and then get worse, and they forget what they named earlier. The result is names that don't agree with each other.

## What happened in this project

- **The project is open about using AI.** It started as a test of a free AI model that can run on your own computer, and people did a lot of the work by hand after that. About a third of the changes so far say they were made with an AI's help.
- **The first naming passes were fast, and some names were wrong.**
  - Two pieces of code were given the same names as parts of the toolkit. That quietly broke the game's sound until someone renamed them. A check was added later so that particular mistake can't happen again.
  - One piece of code was named as if it did one job when it does a different one. That stayed wrong for about two months, until a contributor read it properly. Older notes elsewhere still use the wrong name.
  - One more name is known to be too narrow and hasn't been fixed yet.
- **None of these names said how they were worked out,** so nobody could tell which ones to trust. That's the same problem the study describes.
- **Today, only one rule is checked automatically:** that a name doesn't clash with the toolkit. The other naming rules are written down for people and AI helpers to follow, but nothing checks them.

## What could change

- Every suggested name would need a written reason and a level of confidence before it's accepted. See [function_mapping](../function_mapping/overview.md).
- An automatic check could catch badly formed names, and names with no explanation, before they're added.
- When a name turns out to be wrong, the old name should be removed from the notes too, so nobody (person or AI) learns it again.

People already know this project uses AI. So the best answer to criticism is that every name comes with a note saying how it was worked out, which anyone can check.

## What players need to do

Nothing. This only affects how contributors work.
