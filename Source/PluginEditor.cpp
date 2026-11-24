#include "PluginEditor.h"

SunoFixAudioProcessorEditor::SunoFixAudioProcessorEditor (SunoFixAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setSize (680, 330);

    struct Def { const char* id; const char* name; };
    static const Def defs[7] = {
        { "fix", "FIX AMOUNT" }, { "mud", "DE-MUD" }, { "harsh", "DE-HARSH" },
        { "sib", "DE-FIZZ" },    { "air", "AIR" },    { "width", "WIDTH" },
        { "gain", "OUTPUT" }
    };

    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& k = knobs[i];
        k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
        k.slider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xff35d0ba));
        k.label.setText (defs[i].name, juce::dontSendNotification);
        k.label.setJustificationType (juce::Justification::centred);
        k.label.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        addAndMakeVisible (k.slider);
        addAndMakeVisible (k.label);
        k.att = std::make_unique<SliderAtt> (proc.apvts, defs[i].id, k.slider);
    }

    for (auto* b : { &monoBass, &limiter, &bypass })
        addAndMakeVisible (*b);
    monoAtt   = std::make_unique<ButtonAtt> (proc.apvts, "monobass", monoBass);
    limAtt    = std::make_unique<ButtonAtt> (proc.apvts, "limiter", limiter);
    bypassAtt = std::make_unique<ButtonAtt> (proc.apvts, "bypass", bypass);

    fixButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff35d0ba));
    fixButton.setColour (juce::TextButton::textColourOffId, juce::Colours::black);
    fixButton.onClick = [this]
    {
        proc.startAnalysis();
        status.setText ("Listening... keep the song playing", juce::dontSendNotification);
    };
    addAndMakeVisible (fixButton);

    status.setText ("Press play on your song, then click ONE-CLICK FIX", juce::dontSendNotification);
    status.setJustificationType (juce::Justification::centred);
    status.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible (status);

    startTimerHz (20);
}

void SunoFixAudioProcessorEditor::applyAnalysis (const std::array<float, 4>& r)
{
    auto set = [this] (const char* id, float v)
    {
        if (auto* prm = proc.apvts.getParameter (id))
            prm->setValueNotifyingHost (prm->convertTo0to1 (v));
    };
    set ("fix", 1.0f);
    set ("mud", r[0]);
    set ("harsh", r[1]);
    set ("sib", r[2]);
    set ("air", r[3]);
}

void SunoFixAudioProcessorEditor::timerCallback()
{
    std::array<float, 4> r {};
    if (proc.takeAnalysisResult (r))
    {
        applyAnalysis (r);
        status.setText ("Done - settings tuned to this song. Fine-tune by ear.", juce::dontSendNotification);
        return;
    }

    const float prog = proc.analysisProgress.load();
    if (prog >= 0.0f)
        status.setText ("Listening... " + juce::String (juce::roundToInt (prog * 100.0f)) + "%",
                        juce::dontSendNotification);
}

void SunoFixAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff14181d));
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    g.drawText ("SunoFix", 20, 10, 300, 32, juce::Justification::left);
    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("one-click cleanup for AI-generated songs", 20, 40, 400, 16, juce::Justification::left);
}

void SunoFixAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (20);
    area.removeFromTop (52);

    auto top = area.removeFromTop (44);
    fixButton.setBounds (top.removeFromLeft (220));
    top.removeFromLeft (16);
    status.setBounds (top);

    area.removeFromTop (14);
    auto toggles = area.removeFromBottom (28);
    monoBass.setBounds (toggles.removeFromLeft (120));
    limiter.setBounds (toggles.removeFromLeft (120));
    bypass.setBounds (toggles.removeFromLeft (120));

    const int w = area.getWidth() / (int) knobs.size();
    for (auto& k : knobs)
    {
        auto col = area.removeFromLeft (w);
        k.label.setBounds (col.removeFromTop (18));
        k.slider.setBounds (col);
    }
}
