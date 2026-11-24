#include "PluginProcessor.h"
#include "PluginEditor.h"

// ----------------------------------------------------------------------------
// TUNING CONSTANTS - adjust by ear if you want a stronger / gentler fix
// ----------------------------------------------------------------------------
namespace Tune
{
    constexpr float mudFreq     = 300.0f;   // Hz  - low-mid "boxiness"
    constexpr float mudQ        = 0.9f;
    constexpr float mudMaxCutDb = 4.0f;

    constexpr float harshFreq   = 3800.0f;  // Hz  - AI "shout" / metallic presence
    constexpr float harshQ      = 1.0f;
    constexpr float harshThresh = 0.22f;    // band-to-full-signal energy ratio
    constexpr float harshMaxCut = 6.0f;     // dB

    constexpr float sibFreq     = 7500.0f;  // Hz  - fizzy hats / "sss" / shimmer
    constexpr float sibQ        = 1.2f;
    constexpr float sibThresh   = 0.12f;
    constexpr float sibMaxCut   = 8.0f;     // dB

    constexpr float airMixMax   = 0.05f;    // exciter level at Air = 1
    constexpr float sideHpFreq  = 150.0f;   // Hz  - mono bass below this

    constexpr float analysisSeconds = 10.0f;

    // Typical balanced-mix band levels (dB) relative to the 1 kHz reference band
    constexpr float tgtMud   =   6.0f;
    constexpr float tgtHarsh =  -8.5f;
    constexpr float tgtSib   = -13.0f;
    constexpr float tgtAir   = -17.0f;
}

using Filt = juce::dsp::IIR::Filter<float>;
using Coeffs = juce::dsp::IIR::Coefficients<float>;

static void setC (Filt& f, Coeffs::Ptr c) { f.coefficients = c; f.reset(); }

// ----------------------------------------------------------------------------
juce::AudioProcessorValueTreeState::ParameterLayout SunoFixAudioProcessor::createLayout()
{
    using FP = juce::AudioParameterFloat;
    using NR = juce::NormalisableRange<float>;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    auto addF = [&] (const char* id, const char* name, float lo, float hi, float def)
    {
        p.push_back (std::make_unique<FP> (juce::ParameterID { id, 1 }, name, NR (lo, hi, 0.001f), def));
    };
    auto addB = [&] (const char* id, const char* name, bool def)
    {
        p.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def));
    };

    addF ("fix",   "Fix Amount",  0.0f, 1.0f, 1.0f);
    addF ("mud",   "De-Mud",      0.0f, 1.0f, 0.5f);
    addF ("harsh", "De-Harsh",    0.0f, 1.0f, 0.6f);
    addF ("sib",   "De-Fizz",     0.0f, 1.0f, 0.6f);
    addF ("air",   "Air",         0.0f, 1.0f, 0.3f);
    addF ("width", "Width",       0.8f, 1.5f, 1.15f);
    addF ("gain",  "Output (dB)", -12.0f, 6.0f, 0.0f);
    addB ("monobass", "Mono Bass", true);
    addB ("limiter",  "Limiter",   true);
    addB ("bypass",   "Bypass",    false);

    return { p.begin(), p.end() };
}

SunoFixAudioProcessor::SunoFixAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    pFix      = apvts.getRawParameterValue ("fix");
    pMud      = apvts.getRawParameterValue ("mud");
    pHarsh    = apvts.getRawParameterValue ("harsh");
    pSib      = apvts.getRawParameterValue ("sib");
    pAir      = apvts.getRawParameterValue ("air");
    pWidth    = apvts.getRawParameterValue ("width");
    pGain     = apvts.getRawParameterValue ("gain");
    pMonoBass = apvts.getRawParameterValue ("monobass");
    pLimiter  = apvts.getRawParameterValue ("limiter");
    pBypass   = apvts.getRawParameterValue ("bypass");

    for (auto& r : result) r = 0.0f;
}

bool SunoFixAudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && l.getMainInputChannelSet()  == juce::AudioChannelSet::stereo();
}

void SunoFixAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, 2 };
    limiter.prepare (spec);
    limiter.setThreshold (-1.0f);
    limiter.setRelease (120.0f);

    for (int ch = 0; ch < 2; ++ch)
    {
        setC (hp25[ch],   Coeffs::makeHighPass (sr, 25.0f, 0.7071f));
        setC (mud[ch],    Coeffs::makePeakFilter (sr, Tune::mudFreq, Tune::mudQ, 1.0f));
        setC (airHp1[ch], Coeffs::makeHighPass (sr, 6000.0f, 0.7071f));
        setC (airHp2[ch], Coeffs::makeHighPass (sr, 10000.0f, 0.7071f));
        setC (harshBand.bp[ch], Coeffs::makeBandPass (sr, Tune::harshFreq, Tune::harshQ));
        setC (sibBand.bp[ch],   Coeffs::makeBandPass (sr, Tune::sibFreq,   Tune::sibQ));
    }
    setC (sideHp, Coeffs::makeHighPass (sr, Tune::sideHpFreq, 0.7071f));

    setC (aBP[0], Coeffs::makeBandPass (sr, 1000.0f,  0.7f));
    setC (aBP[1], Coeffs::makeBandPass (sr, Tune::mudFreq,   1.0f));
    setC (aBP[2], Coeffs::makeBandPass (sr, Tune::harshFreq, 1.0f));
    setC (aBP[3], Coeffs::makeBandPass (sr, Tune::sibFreq,   1.2f));
    setC (aBP[4], Coeffs::makeBandPass (sr, 12500.0f, 0.8f));

    harshBand.env = sibBand.env = fullEnv = 0.0f;
    envAtk = std::exp (-1.0f / (float) (sr * 0.003));
    envRel = std::exp (-1.0f / (float) (sr * 0.050));
    lastGain = juce::Decibels::decibelsToGain (pGain->load());
    lastMudDb = 1000.0f; // force coefficient refresh

    targetSamples = (juce::int64) (Tune::analysisSeconds * sr);
    analysing = false;
    analysisProgress = -1.0f;
}

void SunoFixAudioProcessor::runAnalysisBlock (const float* l, const float* r, int n)
{
    double e = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double m = 0.5 * (l[i] + r[i]);
        e += m * m;
    }
    if (std::sqrt (e / juce::jmax (1, n)) < 0.003) // skip silence
        return;

    for (int i = 0; i < n; ++i)
    {
        const float m = 0.5f * (l[i] + r[i]);
        for (int k = 0; k < 5; ++k)
        {
            const float y = aBP[k].processSample (m);
            sumSq[k] += (double) y * y;
        }
    }

    analysedSamples += n;
    analysisProgress = juce::jmin (1.0f, (float) analysedSamples / (float) targetSamples);

    if (analysedSamples >= targetSamples)
    {
        const double cnt = (double) analysedSamples;
        auto dB = [&] (int k) { return 10.0 * std::log10 (sumSq[k] / cnt + 1.0e-12); };
        const double ref = dB (0);

        const float mudEx   = (float) (dB (1) - ref) - Tune::tgtMud;
        const float harshEx = (float) (dB (2) - ref) - Tune::tgtHarsh;
        const float sibEx   = (float) (dB (3) - ref) - Tune::tgtSib;
        const float airDef  = Tune::tgtAir - (float) (dB (4) - ref);

        result[0] = juce::jlimit (0.0f, 1.0f, 0.2f + mudEx   / 6.0f);
        result[1] = juce::jlimit (0.0f, 1.0f, 0.2f + harshEx / 5.0f);
        result[2] = juce::jlimit (0.0f, 1.0f, 0.2f + sibEx   / 6.0f);
        result[3] = juce::jlimit (0.0f, 1.0f, 0.2f + airDef  / 10.0f);

        analysing = false;
        analysisProgress = -1.0f;
        resultReady = true;
    }
}

bool SunoFixAudioProcessor::takeAnalysisResult (std::array<float, 4>& out)
{
    if (! resultReady.exchange (false))
        return false;
    for (size_t i = 0; i < 4; ++i)
        out[i] = result[i].load();
    return true;
}

void SunoFixAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2 || n == 0)
        return;

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);

    // ---- auto-analysis (measures the incoming, unprocessed signal) ----
    if (analysisRequested.exchange (false))
    {
        for (auto& s : sumSq) s = 0.0;
        for (auto& f : aBP) f.reset();
        analysedSamples = 0;
        analysing = true;
        analysisProgress = 0.0f;
    }
    if (analysing)
        runAnalysisBlock (L, R, n);

    if (pBypass->load() > 0.5f)
        return;

    // ---- parameters ----
    const float fix      = pFix->load();
    const float harshAmt = pHarsh->load() * fix;
    const float sibAmt   = pSib->load() * fix;
    const float airMix   = pAir->load() * fix * Tune::airMixMax;
    const float width    = 1.0f + (pWidth->load() - 1.0f) * fix;
    const bool  monoBass = pMonoBass->load() > 0.5f;

    const float mudDb = -Tune::mudMaxCutDb * pMud->load() * fix;
    if (std::abs (mudDb - lastMudDb) > 0.001f)
    {
        for (int ch = 0; ch < 2; ++ch)
            mud[ch].coefficients = Coeffs::makePeakFilter (sr, Tune::mudFreq, Tune::mudQ,
                                                           juce::Decibels::decibelsToGain (mudDb));
        lastMudDb = mudDb;
    }

    // Split-band dynamic cut: compares a band's energy with the whole signal, so it
    // reacts to *tonal imbalance* (level independent) rather than to loudness.
    auto dyn = [this] (DynBand& b, float amt, float thresh, float maxCutDb, float& xl, float& xr)
    {
        const float b0 = b.bp[0].processSample (xl);
        const float b1 = b.bp[1].processSample (xr);
        const float lvl = std::max (std::abs (b0), std::abs (b1));
        b.env = lvl > b.env ? envAtk * b.env + (1.0f - envAtk) * lvl
                            : envRel * b.env + (1.0f - envRel) * lvl;

        if (amt > 0.001f && fullEnv > 1.0e-4f)
        {
            const float rel = b.env / fullEnv;
            if (rel > thresh)
            {
                const float excessDb = 20.0f * std::log10 (rel / thresh);
                const float cutDb = std::min (excessDb * 0.6f, maxCutDb * amt);
                const float g = juce::Decibels::decibelsToGain (-cutDb);
                xl -= b0 * (1.0f - g);
                xr -= b1 * (1.0f - g);
            }
        }
    };

    for (int i = 0; i < n; ++i)
    {
        float xl = hp25[0].processSample (L[i]);
        float xr = hp25[1].processSample (R[i]);

        xl = mud[0].processSample (xl);
        xr = mud[1].processSample (xr);

        const float full = std::max (std::abs (xl), std::abs (xr));
        fullEnv = full > fullEnv ? envAtk * fullEnv + (1.0f - envAtk) * full
                                 : envRel * fullEnv + (1.0f - envRel) * full;

        dyn (harshBand, harshAmt, Tune::harshThresh, Tune::harshMaxCut, xl, xr);
        dyn (sibBand,   sibAmt,   Tune::sibThresh,   Tune::sibMaxCut,   xl, xr);

        // "Air": harmonic exciter on the top octaves (restores sparkle lost to AI low-pass)
        const float h0 = airHp1[0].processSample (xl);
        const float h1 = airHp1[1].processSample (xr);
        const float a0 = airHp2[0].processSample (std::tanh (10.0f * h0));
        const float a1 = airHp2[1].processSample (std::tanh (10.0f * h1));
        xl += a0 * airMix;
        xr += a1 * airMix;

        // Mid/Side: mono the bass, widen the rest
        const float m = 0.5f * (xl + xr);
        float s = 0.5f * (xl - xr);
        const float sFiltered = sideHp.processSample (s);
        if (monoBass) s = sFiltered;
        s *= width;

        L[i] = m + s;
        R[i] = m - s;
    }

    const float g = juce::Decibels::decibelsToGain (pGain->load());
    buffer.applyGainRamp (0, n, lastGain, g);
    lastGain = g;

    if (pLimiter->load() > 0.5f)
    {
        juce::dsp::AudioBlock<float> block (buffer);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        limiter.process (ctx);
    }
}

juce::AudioProcessorEditor* SunoFixAudioProcessor::createEditor()
{
    return new SunoFixAudioProcessorEditor (*this);
}

void SunoFixAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void SunoFixAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SunoFixAudioProcessor();
}
