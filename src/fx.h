#pragma once
#include <Arduino.h>

// CRT motion effects drawn on top of whatever is on screen.
// They read pixels back from the panel, tint them, and put them back, so the UI
// code doesn't need to know about them. Any drawing call must fxRestore() first.

void fxTick();          // rolling band, static specks, glitch lines, corners
void fxRestore();       // put back pixels the effects currently cover
void fxPause(bool p);   // freeze effects (calibration, modal drawing)
bool fxSelfTest();      // can the panel be read back? false = motion fx disabled
void fxSetScene(void (*fn)());  // current screen's full-redraw function (redraw-mode band)
bool fxReadback();             // true = panel readback works (all effects), false = band only
void fxCorners();       // rounded "tube" corners
void fxPowerOn();       // line -> picture
void fxPowerOff();      // picture -> line -> dot -> black
