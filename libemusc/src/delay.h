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


#ifndef __DELAY_H__
#define __DELAY_H__


#include <vector>


namespace EmuSC {


class Delay
{
public:
  Delay(int maxDelay, int delay);
  virtual ~Delay();

  virtual float process_sample(float input);
  void set_feedback(float feedback);
  void set_delay(int time);
  int get_readIndex(void) { return _readIndex; }

protected:
  std::vector<float> _delayLine;
  int _maxDelay;
  int _delay;

  int _readIndex;
  int _writeIndex;

  float _feedbackFactor;

private:
  Delay();
};


}

#endif  // __DELAY_H__
