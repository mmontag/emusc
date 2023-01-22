/*  
 *  This file is part of libEmuSC, a Sound Canvas emulator library
 *  Copyright (C) 2022-2023  Håkon Skjelten
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


#include "part.h"

#include <algorithm>
#include <cmath>
#include <iostream>


namespace EmuSC {

Part::Part(uint8_t id, uint8_t mode, uint8_t type, Settings *settings,
	   ControlRom &ctrlRom, PcmRom &pcmRom)
  : _id(id),
    _settings(settings),
    _ctrlRom(ctrlRom),
    _pcmRom(pcmRom),
    _7bScale(1/127.0)
{
  // TODO: Rename mode => synthMode and set proper defaults for MT32 mode

  reset();
}


Part::~Part()
{
  clear_all_notes();
}


// Parts always produce 2 channel & 32kHz (native) output. Other channel
// numbers and sample rates are handled by the calling Synth class.
int Part::get_next_sample(float *sampleOut)
{
  // Return immediately if we have no notes to play
  if (_notes.size() == 0)
    return 0;

  float partSample[2] = { 0, 0 };
  float accSample = 0;

  // Get next sample from active notes, delete those which are finished
  std::list<Note*>::iterator itr = _notes.begin();
  while (itr != _notes.end()) {
    bool finished = (*itr)->get_next_sample(partSample);

    if (finished) {
//      std::cout << "Both partials have finished -> delete note" << std::endl;
      delete *itr;
      itr = _notes.erase(itr);
    } else {
      ++itr;
    }
  }

  // Apply volume from part (MIDI channel) and expression (CM11)
  uint8_t expression = _settings->get_param(PatchParam::Expression, _id);
  partSample[0] *= _settings->get_param(PatchParam::PartLevel, _id) *
    _7bScale * (expression * _7bScale);
  partSample[1] *= _settings->get_param(PatchParam::PartLevel, _id) *
    _7bScale * (expression * _7bScale);

  // Store last (highest) value for future queries (typically for bar display)
  _lastPeakSample =
    (_lastPeakSample >= partSample[0]) ? _lastPeakSample : partSample[0];
  
  // Apply pan from part (MIDI Channel)
  uint8_t panpot = _settings->get_param(PatchParam::PartPanpot, _id);
  if (panpot > 64)
    partSample[0] *= 1.0 - (panpot - 64) / 63.0;
  else if (panpot < 64)
    partSample[1] *= (panpot - 1) / 64.0;

  sampleOut[0] += partSample[0];
  sampleOut[1] += partSample[1];

  return 0;
}


float Part::get_last_peak_sample(void)
{
  if (_mute)
    return -1;

  float ret = std::abs(_lastPeakSample);
  _lastPeakSample = 0;

  return ret;
}


int Part::get_num_partials(void)
{
  if (_notes.size() == 0)
    return 0;

  int numPartials = 0;
  for (auto &n: _notes)
    numPartials += n->get_num_partials();

  return numPartials;
}


// Should mute => not accept key - or play silently in the background?
int Part::add_note(uint8_t key, uint8_t keyVelocity)
{
  // 1. Check if part is muted or rxNoteMessage is disabled
  // FIXME: Verify that this is correct behavior for mute
  if (_mute || !_settings->get_param(PatchParam::RxNoteMessage, _id))
    return 0;

  // 2. Check if key is outside part configured key range
  if (key < _settings->get_param(PatchParam::KeyRangeLow, _id) ||
      key > _settings->get_param(PatchParam::KeyRangeHigh, _id))
    return 0;

  // 3. Find partial(s) used by instrument or drum set
  uint16_t instrumentIndex;
  int drumSet;
  if (_settings->get_param(PatchParam::UseForRhythm, _id) == mode_Norm) {
    uint8_t toneBank = _settings->get_param(PatchParam::ToneNumber, _id);
    uint8_t toneIndex = _settings->get_param(PatchParam::ToneNumber2,_id);
    instrumentIndex = _ctrlRom.variation(toneBank)[toneIndex];
    drumSet = -1;
  } else {
    instrumentIndex = _ctrlRom.drumSet(_drumSet).preset[key];
    drumSet = _drumSet;
  }

  if (instrumentIndex == 0xffff)        // Ignore undefined instruments / drums
    return 0;

  // 4. If note is a drum -> check if drum accepts note on
  if (drumSet >= 0 && !(_ctrlRom.drumSet(drumSet).flags[key] & 0x10))
    return 0;

  // 5. Calculate corrected key velocity based on velocity sens depth & offset
  //    according to description in SC-55 owner's manual page 38
  uint8_t velSensDepth =
    _settings->get_param(PatchParam::VelocitySenseDepth, _id);
  uint8_t velSensOffset =
    _settings->get_param(PatchParam::VelocitySenseOffset, _id);
  float v = keyVelocity * (velSensDepth / 64.0);
  if (velSensOffset >= 64)
    v += velSensOffset - 64;
  else
    v *= (velSensOffset + 64) / 127.0;
  uint8_t velocity = (v <= 127) ? std::roundf(v) : 127;

  // 6. Remove all existing notes if part is in mono mode according to the
  //    SC-55 owner's manual page 39
  if (_settings->get_param(PatchParam::PolyMode, _id) == false &&
      _settings->get_param(PatchParam::UseForRhythm, _id) == 0)
    clear_all_notes();

  // 7. Create new note and set default values (note: using pointers)
  int8_t keyShift = _settings->get_param(SystemParam::KeyShift) - 0x40 +
    _settings->get_param(PatchParam::PitchKeyShift, _id) - 0x40;

  Note *n = new Note(key, keyShift, velocity, instrumentIndex,
		     drumSet, _ctrlRom, _pcmRom, _settings, _id);
  _notes.push_back(n);

  if (_settings->get_param(PatchParam::Hold1, _id))
      n->sustain(true);

  if (0)
    std::cout << "EmuSC: New note [ part=" << (int) _id
	      << " key=" << (int) key
	      << " velocity=" << (int) velocity
	      << " preset=" << _ctrlRom.instrument(instrumentIndex).name
	      << " ]" << std::endl;
  
    return 1;
}


int Part::stop_note(uint8_t key)
{
  int i;
  for (auto &n : _notes) {
    bool ret = n->stop(key);
    i += ret;
  }

  return i;
}


int Part::control_change(uint8_t msgId, uint8_t value)
{
  if (!_settings->get_param(PatchParam::RxControlChange, _id))
    return 0;

  bool updateGUI = false;

  if (msgId == 0) {                                    // Bank select
    _settings->set_param(PatchParam::ToneNumber, value, _id);

  } else if (msgId == 1) {                             // Modulation
    if (_settings->get_param(PatchParam::RxModulation, _id))
      _settings->set_param(PatchParam::Modulation, value, _id);

  } else if (msgId == 5) {                             // Portamento time
    _settings->set_param(PatchParam::PortamentoTime, value, _id);

  } else if (msgId == 6) {                             // Data entry MSB
    // RPN
    uint8_t msb = _settings->get_param(PatchParam::RPN_MSB, _id);
    uint8_t lsb = _settings->get_param(PatchParam::RPN_LSB, _id);
    if (msb != 0x7f && lsb != 0x7f)
      if (msb == 0 && lsb == 0)                        // Pitch bend range
	_settings->set_param(PatchParam::PB_PitchControl, value, _id);
      else if (msb == 0 && lsb == 1)                   // Master fine tuning
	std::cout << "Master fine tuning (part) not implemented" << std::endl;
      else if (msb == 0 && lsb == 2)                   // Master coarse tuning
	std::cout << "Master coarse tuning (part) not implemented" << std::endl;

    // NRPN
    msb = _settings->get_param(PatchParam::NRPN_MSB, _id);
    lsb = _settings->get_param(PatchParam::NRPN_LSB, _id);
    if (msb != 0x7f && lsb != 0x7f)
      if (msb == 0x01 && lsb == 0x08)
	_settings->set_param(PatchParam::VibratoRate, value, _id);
      else if (msb == 0x01 && lsb == 0x09)
	_settings->set_param(PatchParam::VibratoDepth, value, _id);
      else if (msb == 0x01 && lsb == 0x0a)
	_settings->set_param(PatchParam::VibratoDelay, value, _id);
      else if (msb == 0x01 && lsb == 0x20)
	_settings->set_param(PatchParam::TVFCutoffFreq, value, _id);
      else if (msb == 0x01 && lsb == 0x21)
	_settings->set_param(PatchParam::TVFResonance, value, _id);
      else if (msb == 0x01 && lsb == 0x63)
	_settings->set_param(PatchParam::TVFAEnvAttack, value, _id);
      else if (msb == 0x01 && lsb == 0x64)
	_settings->set_param(PatchParam::TVFAEnvDecay, value, _id);
      else if (msb == 0x01 && lsb == 0x66)
	_settings->set_param(PatchParam::TVFAEnvRelease, value, _id);
      else if (msb == 0x18)
	std::cout << "Pitch coarse (drums) not implemented yet" << std::endl;
      else if (msb == 0x1a)
	std::cout << "TVA level (drums) not implemented yet" << std::endl;
      else if (msb == 0x1c)
	std::cout << "Panpot (drums) not implemented yet" << std::endl;
      else if (msb == 0x1d)
	std::cout << "Reverb (drums) not implemented yet" << std::endl;
    // SC-88 adds Chorus and Delay

  } else if (msgId == 7) {                             // Volume
    if (_settings->get_param(PatchParam::RxVolume, _id)) {
      _settings->set_param(PatchParam::PartLevel, value, _id);
      updateGUI = true;
    }

  } else if (msgId == 10) {                            // Panpot
    if (_settings->get_param(PatchParam::RxPanpot, _id)) {
      _settings->set_param(PatchParam::PartPanpot, value, _id);
      updateGUI = true;
    }

  } else if (msgId == 11) {                            // Expression
    if (_settings->get_param(PatchParam::RxExpression, _id))
      _settings->set_param(PatchParam::Expression, value, _id);

  } else if (msgId == 38) {                            // Data entry LSB
    // Only RPN #1
    uint8_t msb = _settings->get_param(PatchParam::RPN_MSB, _id);
    uint8_t lsb = _settings->get_param(PatchParam::RPN_LSB, _id);
    if (msb == 0 && lsb == 1)
      std::cout << "Master fine tuning (part) not implemented yet" << std::endl;

  } else if (msgId == 64) {                            // Hold1
    if (_settings->get_param(PatchParam::RxHold1, _id)) {
      if (value < 64) {
	_settings->set_param(PatchParam::Hold1, (uint8_t) 0, _id);
      } else {
	_settings->set_param(PatchParam::Hold1, (uint8_t) 1, _id);
      }

      for (auto &n : _notes)
	n->sustain(_settings->get_param(PatchParam::Hold1, _id));

    } // Note: SC-88 Pro seems to use full 7 bit value for Hold1

  } else if (msgId == 65) {                            // Portamento
    if (_settings->get_param(PatchParam::RxPortamento, _id)) {
      if (value < 64)
	_settings->set_param(PatchParam::Portamento, (uint8_t)0,_id);
      else
	_settings->set_param(PatchParam::Portamento, (uint8_t)1,_id);
    }

  } else if (msgId == 66) {                            // Sostenuto
    if (_settings->get_param(PatchParam::RxSostenuto, _id)) {
      if (value < 64)
	_settings->set_param(PatchParam::Sostenuto, (uint8_t)0,_id);
      else
	_settings->set_param(PatchParam::Sostenuto, (uint8_t)1,_id);

      for (auto &n : _notes)
	n->sustain(_settings->get_param(PatchParam::Sostenuto, _id));
    }

  } else if (msgId == 67) {                            // Soft
    if (_settings->get_param(PatchParam::RxSoft, _id)) {
      if (value < 64)
	_settings->set_param(PatchParam::Soft, (uint8_t)0,_id);
      else
	_settings->set_param(PatchParam::Soft, (uint8_t)1,_id);
    }

  } else if (msgId == 91) {                            // Reverb
    _settings->set_param(PatchParam::ReverbSendLevel, value, _id);
    updateGUI = true;

  } else if (msgId == 93) {                            // Chorus
    _settings->set_param(PatchParam::ChorusSendLevel, value, _id);
    updateGUI = true;

  } else if (msgId == 98) {                            // NRPN LSB
    if (_settings->get_param(PatchParam::RxNRPN, _id))
      _settings->set_param(PatchParam::NRPN_LSB, value, _id);

  } else if (msgId == 99) {                            // NRPN MSB
    if (_settings->get_param(PatchParam::RxNRPN, _id))
      _settings->set_param(PatchParam::NRPN_MSB, value, _id);

  } else if (msgId == 100) {                           // RPN LSB
    if (_settings->get_param(PatchParam::RxRPN, _id))
      _settings->set_param(PatchParam::RPN_LSB, value, _id);

  } else if (msgId == 101) {                           // RPN MSB
    if (_settings->get_param(PatchParam::RxRPN, _id))
      _settings->set_param(PatchParam::RPN_MSB, value, _id);

  } else if (msgId == 120 || msgId == 123 || msgId == 124 ||
	     msgId == 125 || msgId == 126 || msgId == 127) {
    clear_all_notes();
  }

  // Update CC1 and CC2 based on configured controller inputs
  if (_settings->get_param(PatchParam::CC1ControllerNumber, _id) == msgId)
    _settings->set_param(PatchParam::CC1Controller, value, _id);

  if (_settings->get_param(PatchParam::CC2ControllerNumber, _id) == msgId)
    _settings->set_param(PatchParam::CC2Controller, value, _id);

  return updateGUI;
}


int Part::poly_key_pressure(uint8_t key, uint8_t value)
{
  std::cout << "Polyphonic key pressure not implemented (ch="
	    << _settings->get_param(PatchParam::RxChannel, _id)
	    << ", key=" << (int) key << ", value=" << value << ")"
	    << std::endl;

  return 0;
}


int Part::channel_pressure(uint8_t value)
{
  if (_settings->get_param(PatchParam::RxChPressure, _id))
    _settings->set_param(PatchParam::ChannelPressure, value, _id);

  return 0;
}


int Part::pitch_bend_change(uint8_t lsb, uint8_t msb)
{
  if (!_settings->get_param(PatchParam::RxPitchBend, _id))
    return -1;
  
  _settings->set_patch_param((uint16_t) PatchParam::PitchBend,
			     (uint8_t) ((msb & 0x7f) >> 1), _id);

  // SC-55 line has 12 bit resolution on pitch wheel (that is 14 bit)
  //	  if (_ctrlRom.synthModel == ControlRom::ss_SC55)
  _settings->set_patch_param((uint16_t) PatchParam::PitchBend + 1,
			     (uint8_t) ((lsb & 0x7c) | (msb << 7)), _id);
  //	    else               // SC-88 line has normal 14 bit resolution
  //	    set_patch_param((uint16_t) PatchParam::PitchBend + 1,
  //			    (uint8_t) ((lsb & 0x7f) | (msb << 7)), _id);

  return 0;
}


int Part::clear_all_notes(void)
{
  int i = _notes.size();
  for (auto n : _notes)
    delete n;

  _notes.clear();

  return i;
}


// TODO: Remove all unnecessary variables and initialization
void Part::reset(void)
{
  clear_all_notes();

  _drumSet = 0;
  _partialReserve = 2;

  _mute = false;
  _lastPeakSample = 0;
}


// [index, bank] is the [x,y] coordinate in the variation table
// For drum sets, index is the program number in the drum set bank
int Part::set_program(uint8_t index)
{
  if (!_settings->get_param(PatchParam::RxProgramChange, _id))
    return -1;

  _settings->set_param(PatchParam::ToneNumber2, index, _id);

  uint8_t bank = _settings->get_param(PatchParam::ToneNumber, _id);

  // Finds correct instrument variation from variations table
  // Implemented according to SC-55 Owner's Manual page 42-45
  if (_settings->get_param(PatchParam::UseForRhythm, _id) == mode_Norm) {
    uint16_t instrument = _ctrlRom.variation(bank)[index];
    if (bank < 63 && index < 120) {
      while (instrument == 0xffff)
	instrument = _ctrlRom.variation(--bank)[index];

      _settings->set_param(PatchParam::ToneNumber, bank, _id);
    }

  // If part is used for drums, select correct drum set
  } else {
    const std::vector<int> &drumSetBank = _ctrlRom.drum_set_bank();
    std::vector<int>::const_iterator it = std::find(drumSetBank.begin(),
						    drumSetBank.end(),
						    (int) index);
    if (it != drumSetBank.end()) {
      _drumSet = (int8_t) std::distance(drumSetBank.begin(), it);
    } else {
      std::cerr << "EmuSC: Illegal program for drum set (" << (int) index << ")"
		<< std::endl;
      return 0;
    }
  }

  return 1;
}


uint16_t Part::_native_endian_uint16(uint8_t *ptr)
{
  if (_le_native())
    return (ptr[0] << 8 | ptr[1]);

  return (ptr[1] << 8 | ptr[0]);
}

}
