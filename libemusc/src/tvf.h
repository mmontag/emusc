/*  
 *  This file is part of libEmuSC, a Sound Canvas emulator library
 *  Copyright (C) 2022-2024  Håkon Skjelten
 *
 *  libEmuSC is free software: you can redistribute it and/or modify it
 *  under the terms of the GNU Lesser General Public License as published
 *  by the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  libEmuSC is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with libEmuSC. If not, see <http://www.gnu.org/licenses/>.
 */


#ifndef __TVF_H__
#define __TVF_H__


#include "control_rom.h"
#include "lowpass_filter.h"
#include "ahdsr.h"
#include "settings.h"
#include "wave_generator.h"

#include <stdint.h>

#include <array>


namespace EmuSC {


class TVF
{
public:
  TVF(ControlRom::InstPartial &instPartial, uint8_t key, WaveGenerator *_LFO[2],
      Settings *settings, int8_t partId);
  ~TVF();

  double apply(double input);
  void note_off();

  inline bool finished(void) { if (_ahdsr) return _ahdsr->finished(); }

private:
  uint32_t _sampleRate;

  WaveGenerator *_LFO1;
  WaveGenerator *_LFO2;
  int _LFO1DepthPartial;

  Settings *_settings;
  int8_t _partId;

  AHDSR *_ahdsr;
  LowPassFilter *_lpFilter;

  uint32_t _lpBaseFrequency;
  float _lpResonance;

  ControlRom::InstPartial _instPartial;

  TVF();

};

}

#endif  // __TVF_H__
