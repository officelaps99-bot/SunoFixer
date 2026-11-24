#pragma once
#include "PluginProcessor.h"
#include <array>
#include <memory>

class SunoFixAudioProcessorEditor : public juce::AudioProcessorEditor,
                                    private juce::Timer
{
public:
    explicit SunoFixAudioProcessorEditor (SunoFixAudioProcessor&);
    ~SunoFixAudioProcessorEditor() override { stopTimer(); }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void applyAnalysis (const std::array<float, 4>& r);

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAtt> att;
    };

    SunoFixAudioProcessor& proc;

    juce::TextButton fixButton { "ONE-CLICK FIX" };
    juce::Label status;
    std::array<Knob, 7> knobs;

    juce::ToggleButton monoBass { "Mono Bass" }, limiter { "Limiter" }, bypass { "Bypass" };
    std::unique_ptr<ButtonAtt> monoAtt, limAtt, bypassAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SunoFixAudioProcessorEditor)
};
