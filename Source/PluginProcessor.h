#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>

class SunoFixAudioProcessor : public juce::AudioProcessor
{
public:
    SunoFixAudioProcessor();
    ~SunoFixAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "SunoFix"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- One-click auto-fix -------------------------------------------------
    // Press play on the song, then call startAnalysis(). After ~10 s of audio
    // the processor has measured the spectrum and a result is ready.
    void startAnalysis() { analysisRequested = true; }
    bool takeAnalysisResult (std::array<float, 4>& out); // mud, harsh, sib, air (0..1)
    std::atomic<float> analysisProgress { -1.0f };       // -1 idle, 0..1 running

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    using Filt = juce::dsp::IIR::Filter<float>;
    using Coeffs = juce::dsp::IIR::Coefficients<float>;

    struct DynBand
    {
        Filt bp[2];
        float env = 0.0f;
    };

    void updateCoefficients();
    void runAnalysisBlock (const float* l, const float* r, int n);

    double sr = 44100.0;

    // parameters
    std::atomic<float>* pFix = nullptr;
    std::atomic<float>* pMud = nullptr;
    std::atomic<float>* pHarsh = nullptr;
    std::atomic<float>* pSib = nullptr;
    std::atomic<float>* pAir = nullptr;
    std::atomic<float>* pWidth = nullptr;
    std::atomic<float>* pGain = nullptr;
    std::atomic<float>* pMonoBass = nullptr;
    std::atomic<float>* pLimiter = nullptr;
    std::atomic<float>* pBypass = nullptr;

    // DSP
    Filt hp25[2], mud[2], airHp1[2], airHp2[2], sideHp;
    DynBand harshBand, sibBand;
    float fullEnv = 0.0f;
    float envAtk = 0.0f, envRel = 0.0f;
    float lastGain = 1.0f;
    float lastMudDb = 1000.0f;
    juce::dsp::Limiter<float> limiter;

    // analysis
    std::atomic<bool> analysisRequested { false };
    std::atomic<bool> resultReady { false };
    std::array<std::atomic<float>, 4> result;
    bool analysing = false;
    Filt aBP[5]; // 0 ref(1k) 1 mud 2 harsh 3 sib 4 air
    double sumSq[5] = {};
    juce::int64 analysedSamples = 0;
    juce::int64 targetSamples = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SunoFixAudioProcessor)
};
