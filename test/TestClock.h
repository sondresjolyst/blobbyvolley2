#pragma once

// The tests replace RakNet::GetTime with a clock they can move, see TestClock.cpp.
// It runs at the speed of a steady clock. Every test that moves it puts it back.

/// Moves the clock of RakNet so that it reads now.
void setRakNetTime(unsigned int now);

/// Moves the clock of RakNet ahead by ms.
void advanceRakNetTime(unsigned int ms);

/// Puts the clock of RakNet back to the steady clock.
void resetRakNetTime();
