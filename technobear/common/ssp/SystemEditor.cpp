#include "SystemEditor.h"

#include "BaseProcessor.h"
#include "ssp/Log.h"
// #include "SSP.h"

namespace ssp {

// static constexpr unsigned menuTopY = 200 - 1;
static constexpr unsigned btnTopY = 380 - 1;
static constexpr unsigned btnSpaceY = 50;

inline bool isInternalMidi(const String& name) {
    return name.contains("Juce") || name.contains("Midi Through Port");
}

SystemEditor::SystemEditor(BaseProcessor* p)
    : baseProcessor_(p),
      learnBtn_(
          "Learn", [&](bool b) { midiLearn(b); }, 12 * COMPACT_UI_SCALE, Colours::yellow),
      delBtn_(
          "Delete", [&](bool b) { deleteAutomation(b); }, 12 * COMPACT_UI_SCALE, Colours::yellow),
      noteInputBtn_(
          "Note In", [&](bool b) { noteInput(b); }, 12 * COMPACT_UI_SCALE, Colours::lightskyblue),
      midiInCtrl_("Midi IN", [&](float idx, const std::string& str) { midiInCallback(idx, str); }),
      midiOutCtrl_("Midi OUT", [&](float idx, const std::string& str) { midiOutCallback(idx, str); }),
      midiChannelCtrl_("Midi Channel", [&](float idx, const std::string& str) { midiChannelCallback(idx, str); }),
      deviceMode_(
          "Device",
          [&](bool b) {
              if (!b) mode(M_DEVICE);
          },
          12 * COMPACT_UI_SCALE, Colours::yellow),
      paramMode_(
          "Param",
          [&](bool b) {
              if (!b) mode(M_PARAM);
          },
          12 * COMPACT_UI_SCALE, Colours::yellow),

      mode_(M_PARAM) {
    learnBtn_.setToggle(true);
    noteInputBtn_.setToggle(true);

    // this is not currently working on ssp/linux
    // mdlConnection_ = MidiDeviceListConnection::make ([] {
    //     ssp::log("mdl callback");
    //     auto in = MidiInput::getAvailableDevices();
    //     for (int i = 0; i < in.size(); i++) {
    //         ssp::log(("Midi Input : " + in[i].name).toStdString());
    //     }

    // });


    baseProcessor_->midiLearn(false);

    populateMidiDevices();

    midiChStr_.push_back("OMNI");
    for (int i = 0; i < 16; i++) { midiChStr_.push_back(String(i + 1).toStdString()); }
    midiChannelCtrl_.setValues(midiChStr_, baseProcessor_->midiChannel());

    addAndMakeVisible(midiInCtrl_);
    addAndMakeVisible(midiOutCtrl_);
    addAndMakeVisible(midiChannelCtrl_);


    addAndMakeVisible(noteInputBtn_);
    addAndMakeVisible(learnBtn_);
    addAndMakeVisible(delBtn_);
    addAndMakeVisible(deviceMode_);
    addAndMakeVisible(paramMode_);
    mode(M_PARAM);

    noteInputBtn_.value(baseProcessor_->noteInput());

    selIdx_ = 0;
    idxOffset_ = 0;
}

void SystemEditor::populateMidiDevices() {
    inDevices_.clear();
    midiInStr_.clear();

    outDevices_.clear();
    midiOutStr_.clear();

    auto in = MidiInput::getAvailableDevices();
    int selIdx = -1;
    midiInStr_.push_back("NONE");
    int idx = 0;
    for (int i = 0; i < in.size(); i++) {
        auto name = in[i].name.toStdString();
        auto id = in[i].identifier.toStdString();
        // ssp::log(("Midi Input : " + name);
        if (!isInternalMidi(name)) {
            inDevices_.push_back(in[i]);
            midiInStr_.push_back(std::to_string(idx) + ":" + name);
            if (baseProcessor_->isActiveMidiIn(id)) {
                selIdx = idx + 1;  // none
                // selected is valid, but not connected, attempt reconnect
                if (!baseProcessor_->isConnectedMidiIn(id)) {  // TODO - midi needed ?
                    baseProcessor_->setMidiInDevice(id);
                }
            }
            idx++;
        }
    }

    if (selIdx == -1) {
        auto id = baseProcessor_->getMidiInId();
        if (!id.empty()) {
            midiInStr_.push_back(std::to_string(idx) + ":" + id + " ! ");
            selIdx = idx + 1;
        }
    }

    midiInCtrl_.setValues(midiInStr_, selIdx);

    selIdx = -1;
    idx = 0;
    auto out = MidiOutput::getAvailableDevices();
    midiOutStr_.push_back("NONE");
    for (int i = 0; i < out.size(); i++) {
        auto name = out[i].name.toStdString();
        auto id = in[i].identifier.toStdString();
        // ssp::log(("Midi Output : " + mame);
        if (!isInternalMidi(name)) {
            outDevices_.push_back(out[i]);
            midiOutStr_.push_back(std::to_string(idx) + ":" + name);
            if (baseProcessor_->isActiveMidiOut(id)) {
                selIdx = idx + 1;  // none
                // selected is valid, but not connected, attempt reconnect
                if (!baseProcessor_->isConnectedMidiOut(id)) {  // TODO - midi needed ?
                    baseProcessor_->setMidiOutDevice(id);
                }
            }
            idx++;
        }
    }
    if (selIdx == -1) {
        auto id = baseProcessor_->getMidiOutId();
        if (!id.empty()) {
            midiOutStr_.push_back(std::to_string(idx) + ":" + id + " ! ");
            selIdx = idx + 1;
        }
    }

    midiOutCtrl_.setValues(midiOutStr_, selIdx);
}

void SystemEditor::visibilityChanged() {
    populateMidiDevices();
}


void SystemEditor::mode(UI_Mode m) {
    mode_ = m;
    switch (mode_) {
        case M_PARAM: {
            delBtn_.setVisible(true);
            break;
        }
        case M_DEVICE: {
            delBtn_.setVisible(false);
            break;
        }
    }
    deviceMode_.setVisible(mode_ == M_PARAM);
    paramMode_.setVisible(mode_ == M_DEVICE);
}


void SystemEditor::midiInCallback(float idx, const std::string& dev) {
    //    Logger::writeToLog("midiInCallback -> " + String(idx) + " : " + dev);
    int i = idx - 1; // 0 ==  NONE and available
    if (i >= 0 && i < inDevices_.size()) { 
        auto device = inDevices_[i];
        if (!isInternalMidi(device.name)) {
            baseProcessor_->setMidiInDevice(device.identifier.toStdString());
            return;
        }
    } else {
        // none, disconnect, unavailable leave 'as is', reconnect thread
        if (idx == 0) baseProcessor_->setMidiInDevice("");
    }
}

void SystemEditor::midiOutCallback(float idx, const std::string& dev) {
    //    Logger::writeToLog("midiOutCallback -> " + String(idx) + " : " + dev);
    int i = idx - 1; // 0 ==  NONE and available
    if (i >= 0 && i < outDevices_.size()) {  
        auto device = outDevices_[i];
        if (!isInternalMidi(device.name)) {
            baseProcessor_->setMidiOutDevice(device.identifier.toStdString());
            return;
        }
    } else {
        // none, disconnect, unavailable leave 'as is', reconnect thread
        if (idx == 0) baseProcessor_->setMidiOutDevice("");
    }
}


void SystemEditor::midiChannelCallback(float idx, const std::string& ch) {
    baseProcessor_->midiChannel(idx);
}


SystemEditor::~SystemEditor() {
    baseProcessor_->midiLearn(false);
}

void SystemEditor::midiLearn(bool b) {
    baseProcessor_->midiLearn(b);
}

void SystemEditor::noteInput(bool b) {
    baseProcessor_->noteInput(b);
}

void SystemEditor::deleteAutomation(bool b) {
    if (!b) {
        if (selIdx_ >= 0) {
            auto& am = baseProcessor_->midiAutomation();
            if (am.empty() || selIdx_ >= am.size()) return;

            int idx = 0;
            for (auto ai = am.begin(); ai != am.end(); ai++) {
                auto& a = ai->second;
                if (idx == selIdx_) {
                    am.erase(a.paramIdx_);
                    if (selIdx_ != 0) {
                        selIdx_--;
                        if (selIdx_ < idxOffset_) idxOffset_ = selIdx_;
                    }
                    break;
                }
                idx++;
            }
        }
    }
}

void SystemEditor::onEncoder(unsigned enc, float v) {
    if (mode() == M_PARAM) {
        switch (enc) {
            case 0: {
                auto amsize = baseProcessor_->midiAutomation().size();
                if (amsize == 0) return;

                if (v > 0) {
                    if (selIdx_ < amsize - 1) {
                        selIdx_++;
                        if (selIdx_ >= idxOffset_ + MAX_SHOWN) idxOffset_ = selIdx_ - (MAX_SHOWN - 1);
                    }
                } else {
                    if (selIdx_ > 0) {
                        selIdx_--;
                        if (selIdx_ < idxOffset_) idxOffset_ = selIdx_;
                    }
                }
                break;
            }
            case 1: {
                auto& am = baseProcessor_->midiAutomation();
                if (selIdx_ < am.size()) {
                    auto ai = am.begin();
                    int idx = 0;
                    while (idx < selIdx_) {
                        if (ai != am.end()) ai++;
                        idx++;
                    }
                    if (ai != am.end()) {
                        auto& a = ai->second;
                        a.scale_ += v * 0.01f;
                    }
                }
                break;
            }
            case 2: {
                auto& am = baseProcessor_->midiAutomation();
                if (selIdx_ < am.size()) {
                    auto ai = am.begin();
                    int idx = 0;
                    while (idx < selIdx_) {
                        if (ai != am.end()) ai++;
                        idx++;
                    }
                    if (ai != am.end()) {
                        auto& a = ai->second;
                        a.offset_ += v * 0.01f;
                    }
                }
                break;
            }
            default: break;
        }
    } else {
        switch (enc) {
            case 0: {
                if (v > 0)
                    midiChannelCtrl_.inc(false);
                else
                    midiChannelCtrl_.dec(false);
                break;
            }
            case 1: {
                if (v > 0)
                    midiInCtrl_.inc(false);
                else
                    midiInCtrl_.dec(false);
                break;
            }
            case 2: {
                if (v > 0)
                    midiOutCtrl_.inc(false);
                else
                    midiOutCtrl_.dec(false);
                break;
            }
            case 3: {
                break;
            }
            default: {
                ;
            }
        }
    }
}

void SystemEditor::onEncoderSwitch(unsigned enc, bool v) {
}

void SystemEditor::onButton(unsigned btn, bool v) {
    switch (btn) {
        case 0: {
            learnBtn_.onButton(v);
            break;
        }
        case 1: {
            noteInputBtn_.onButton(v);
            break;
        }
        case 3: {
            if (mode() == M_PARAM)
                deviceMode_.onButton(v);
            else
                paramMode_.onButton(v);
            break;
        }
        case 4: {
            delBtn_.onButton(v);
            break;
        }
        default: break;
    }
}


}  // namespace ssp
