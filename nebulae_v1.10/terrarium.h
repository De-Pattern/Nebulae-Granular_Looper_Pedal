// PedalPCB Terrarium Rev 2 pin map for libDaisy.
// Verified against the Rev 2 schematic/silkscreen and the DaisyWiki pinout CSV.
// Rule: PedalPCB "GPIOn" == libDaisy D(n-1). ADC channel names cross-match on both docs.
#pragma once
#include "daisy_seed.h"

namespace terrarium {
using namespace daisy;
using namespace daisy::seed;

// Pots -> ADC.  PedalPCB GPIO17..22  ==  D16..D21  ==  A1..A6
static const Pin PIN_POT[6] = { A1, A2, A3, A4, A5, A6 };

// Toggles.  SW1=GPIO11->D10, SW2=GPIO10->D9, SW3=GPIO9->D8, SW4=GPIO8->D7
static const Pin PIN_SW[4]  = { D10, D9, D8, D7 };

// Footswitches.  FS1=GPIO26->D25, FS2=GPIO27->D26
static const Pin PIN_FS[2]  = { D25, D26 };

// LEDs.  LED1=GPIO23->D22, LED2=GPIO24->D23
static const Pin PIN_LED[2] = { D22, D23 };
} // namespace terrarium
