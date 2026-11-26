#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

#include <cmath>
#include <iostream>
#include <memory>

/**
    Headless UI screenshot tool (BUILD_UI_SNAPSHOT=ON).

    Renders the real WaveLensEditor through the JUCE software renderer at the
    three supported window sizes and writes PNG files - no display required.
    Synthetic drum samples are generated, loaded through the processor's real
    sample-loading path and a few parameters are set so the screenshots show
    genuine waveform + envelope data.
*/
namespace
{
juce::Component* findComponentByName (juce::Component* parent, const juce::String& name)
{
    if (parent == nullptr)
        return nullptr;
    if (parent->getName() == name)
        return parent;
    for (auto* child : parent->getChildren())
        if (auto* found = findComponentByName (child, name))
            return found;
    return nullptr;
}

juce::File makeSampleFile (const juce::File& dir, const juce::String& name, int kind)
{
    const double sr = 44100.0;
    const int len = (int) (sr * 0.85);
    juce::AudioBuffer<float> buf (1, len);
    double phase = 0.0;

    for (int i = 0; i < len; ++i)
    {
        const double t = (double) i / sr;
        double v = 0.0;

        if (kind == 0) // kick-like swept sine
        {
            const double f = 46.0 + 92.0 * std::exp (-t * 10.0);
            phase += juce::MathConstants<double>::twoPi * f / sr;
            v = 0.92 * std::sin (phase) * std::exp (-t * 6.5);
        }
        else if (kind == 1) // hat-like noise tick
        {
            const double noise = (double) (std::rand() % 2001 - 1000) / 1000.0;
            v = 0.55 * noise * std::exp (-t * 34.0) + 0.18 * std::sin (t * 9500.0) * std::exp (-t * 46.0);
        }
        else // tonal blip
        {
            phase += juce::MathConstants<double>::twoPi * 320.0 / sr;
            v = 0.5 * std::sin (phase) * std::exp (-t * 11.0)
              + 0.12 * std::sin (phase * 2.0) * std::exp (-t * 16.0);
        }

        buf.setSample (0, i, (float) juce::jlimit (-1.0, 1.0, v));
    }

    const auto file = dir.getChildFile (name + ".wav");
    file.deleteFile();
    juce::WavAudioFormat format;
    auto* rawStream = file.createOutputStream().release();
    std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (rawStream, sr, 1, 16, {}, 0));
    if (writer != nullptr)
    {
        writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
        writer->flush();
    }
    return file;
}
} // namespace

int main (int argc, char** argv)
{
    const juce::String outDirPath = argc > 1 ? argv[1] : ".";
    const auto outDir = juce::File::getCurrentWorkingDirectory().getChildFile (outDirPath);
    outDir.createDirectory();

    juce::ScopedJuceInitialiser_GUI juceInit;

    WaveLensProcessor processor;

    // Load synthetic samples through the real loading path
    const auto kick = makeSampleFile (outDir, "ui-shot-kick", 0);
    const auto hat  = makeSampleFile (outDir, "ui-shot-hat", 1);
    const auto blip = makeSampleFile (outDir, "ui-shot-blip", 2);

    const juce::File files[] = { kick, kick, blip, hat, hat, blip };
    for (int l = 0; l < WaveLensProcessor::kMaxSignals; ++l)
    {
        juce::String err;
        if (! processor.loadSampleFile (l, files[(size_t) l], err))
            std::cerr << "sample load failed for layer " << l << ": " << err << std::endl;
    }

    auto setParam = [&processor] (const juce::String& id, float plainValue)
    {
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (processor.apvts.getParameter (id)))
            p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
    };

    // A little parameter variety so tiles/envelopes render real values
    for (int l = 0; l < WaveLensProcessor::kMaxSignals; ++l)
    {
        setParam (ids::layerEnvEnabled (l), 1.0f);
        setParam (ids::layerAttack (l), 0.5f + 3.0f * (float) l);
        setParam (ids::layerHold (l), 2.0f);
        setParam (ids::layerDecay (l), 60.0f + 45.0f * (float) l);
        setParam (ids::layerSustain (l), 0.35f + 0.12f * (float) l);
        setParam (ids::layerRelease (l), 120.0f + 80.0f * (float) l);
    }
    setParam (ids::layerPitch (1), -12.0f);
    setParam (ids::layerPitch (2), 7.0f);
    setParam (ids::layerPitch (4), -3.0f);
    setParam (ids::layerPan (1), -0.35f);
    setParam (ids::layerPan (2), 0.35f);
    setParam (ids::layerPan (4), 0.25f);
    setParam (ids::layerWidth (1), 0.65f);
    setParam (ids::layerWidth (4), 1.35f);
    setParam (ids::layerPogo (4), 0.45f);
    setParam (ids::layerDelay (1), -8.0f);
    setParam (ids::layerDelay (4), 12.0f);
    setParam (ids::layerTrimStart (1), 0.04f);
    setParam (ids::layerTrimEnd (1), 0.86f);
    setParam (ids::layerFadeOut (2), 30.0f);
    setParam (ids::layerHighPass (5), 320.0f);
    setParam (ids::layerLowPass (4), 8.5e3f);
    setParam (ids::preRollMs, 50.0f);

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());

    auto shoot = [&] (const char* fileName, int w, int h)
    {
        editor->setBounds (0, 0, w, h);
        juce::Image img (juce::Image::ARGB, w, h, true);
        {
            juce::Graphics g (img);
            editor->paintEntireComponent (g, true);
        }
        const auto file = outDir.getChildFile (fileName);
        file.deleteFile();
        juce::FileOutputStream out (file);
        if (juce::PNGImageFormat().writeImageToStream (img, out))
            std::cout << "wrote " << file.getFullPathName() << std::endl;
        else
            std::cerr << "failed to write " << file.getFullPathName() << std::endl;
    };

    // Validate the three 4:3 editor presets against the same 800 x 600 SVG canvas.
    shoot ("snapback-ui-extra-large-800x600.png", 800, 600);
    shoot ("snapback-ui-normal-1024x768.png", 1024, 768);
    shoot ("snapback-ui-large-1440x1080.png", 1440, 1080);

    // Bonus shot: global Master M overlay (all six traces) at Normal size.
    if (auto* master = findComponentByName (editor.get(), "Master"))
        if (auto* masterButton = dynamic_cast<juce::Button*> (master))
            masterButton->setToggleState (true, juce::dontSendNotification);
    shoot ("snapback-ui-master-1024x768.png", 1024, 768);

    return 0;
}