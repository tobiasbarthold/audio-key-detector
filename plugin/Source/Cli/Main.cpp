// keydetect-cpp: command line front end of the C++ engine, used to check it against the Python
// reference (plugin/tests/compare_vectors.py). Prints a JSON array like `keydetect --json`
// plus the hpcp and correlations.
//
// Copyright (C) 2026 Tobias Barthold. GNU Affero General Public License v3.0.

#include "../Engine/AudioPrep.h"
#include "../Engine/KeyEngine.h"

#include <juce_core/juce_core.h>

int main (int argc, char* argv[])
{
    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (juce::String::fromUTF8 (argv[i]));
    if (args.isEmpty())
    {
        std::fprintf (stderr, "usage: keydetect-cpp <files or folders...>\n");
        return 1;
    }
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    kd::KeyEngine engine;
    int status = 0;
    juce::Array<juce::var> out;
    for (auto& f : kd::collectAudioFiles (args))
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("file", f.getFullPathName());
        auto audio = kd::loadAudioFile (f, formats);
        if (audio.error.isNotEmpty())
        {
            obj->setProperty ("error", audio.error);
            status = 1;
        }
        else
        {
            auto r = engine.analyze (audio.samples.data(), audio.samples.size());
            auto str = [] (const std::string& s) { return s.empty() ? juce::var() : juce::var (juce::String (s)); };
            obj->setProperty ("clear", r.clear);
            obj->setProperty ("key", str (r.key));
            obj->setProperty ("camelot", str (r.camelot));
            obj->setProperty ("confidence", r.confidence);
            obj->setProperty ("second_key", str (r.secondKey));
            obj->setProperty ("second_camelot", str (r.secondCamelot));
            obj->setProperty ("strength", r.strength);
            obj->setProperty ("tonality", r.tonality);
            obj->setProperty ("reason", juce::String (r.reason));
            obj->setProperty ("best_guess", str (r.bestGuess));
            juce::Array<juce::var> h, c;
            for (auto v : r.hpcp) h.add (v);
            for (auto v : r.correlations) c.add (v);
            obj->setProperty ("hpcp", h);
            obj->setProperty ("correlations", c);
            obj->setProperty ("samples", (juce::int64) audio.samples.size());
        }
        out.add (juce::var (obj));
    }
    std::printf ("%s\n", juce::JSON::toString (juce::var (out)).toRawUTF8());
    return status;
}
