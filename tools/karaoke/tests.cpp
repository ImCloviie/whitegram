#include <QtCore/QCoreApplication>
#include <QtCore/QString>
#include <iostream>
#include <cstdlib>

#include "ayu/ui/utils/lyrics_engine.h"
#include "ayu/ui/utils/lyrics_align.h"
#include "ayu/ui/utils/lyrics_clock.h"
#include "ayu/ui/utils/net_fetch.h"

namespace Ayu::Ui::Net {
bool fixtureEnabled = false;
QByteArray FetchSync(const Request &request) {
    if (!fixtureEnabled) {
        std::cerr << "Unexpected network access in timing tests\n";
        std::abort();
    }
    if (request.url.host() != u"lrclib.net"_q) return {};
    const auto record = QByteArray(R"({"trackName":"Song","artistName":"Artist","duration":119,"syncedLyrics":"[00:01.000]First line\n[00:03.000]Second line"})");
    return request.url.path().endsWith(u"search"_q) ? QByteArray("[") + record + "]" : record;
}
}

void check(bool condition, const char *message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    using namespace Ayu::Ui::Lyrics;
    check(PlaybackPosition(1000, 1000, 500, 550, 1., true) == 1050, "fresh audio clock interpolates smoothly");
    check(PlaybackPosition(1000, 1000, 500, 550, 2., true) == 1100, "playback speed follows the audio clock");
    check(PlaybackPosition(1000, 1000, 500, 550, 1., false) == 1000, "pause and buffering freeze lyrics");
    check(PlaybackPosition(1000, 1000, 500, 900, 1., true) == 1000, "stale samples cannot drift");
    check(PlaybackPosition(10000, 1000, 500, 550, 1., true) == 10000, "seek discards the old clock sample");
    auto data = ParseTimedText(u"[offset:500]\n[00:02.000]<00:02.000>Hello <00:02.300><00:03.000>world<00:03.500>\n[00:05.000]Next line"_q);
    check(data.synced && !data.wordSynced, "mixed LRC is not fully word-synced");
    check(ParseTimedText(u"[00:02.000]<00:02.000>Hello<00:02.300>"_q).wordSynced, "enhanced LRC detected");
    check(data.lines[0].ms == 1500, "LRC offset applied");
    check(data.lines[0].words[0].endMs == 1800, "word pause preserved");
    check(data.lines[0].words[1].startMs == 2500, "word offset applied");
    check(ActiveLineIndex(data, 1499) == -1, "intro remains inactive");
    check(ActiveLineIndex(data, 1500) == 0, "exact line boundary");
    check(WordProgress(data.lines[0].words[0], 1600) > 0.3, "word progress follows media time");
    auto srt = ParseTimedText(u"1\n00:00:01,250 --> 00:00:03,750\nHello <b>world</b>\n\n2\n00:00:05,000 --> 00:00:06,000\nNext\n"_q);
    check(srt.lines.size() == 2 && srt.lines[0].ms == 1250, "SRT timestamps");
    check(srt.lines[0].text == u"Hello world"_q, "SRT markup");
    check(srt.lines[0].endMs == 3750, "SRT cue end is authoritative");
    auto vtt = ParseTimedText(u"WEBVTT\n\ncue\n00:01.500 --> 00:04.000 align:start\nOne two\n"_q);
    check(vtt.lines.size() == 1 && vtt.lines[0].ms == 1500, "WebVTT short timestamp");
    auto dense = ParseTimedText(u"[00:01.000]one two three four five six\n[00:01.100]next"_q);
    check(dense.lines[0].words.back().endMs <= 1100, "dense words cannot cross next line");
    auto analysis = Ayu::Ui::Analysis::Result();
    analysis.ok = true; analysis.frameMs = 20.;
    analysis.voice.assign(1000, 255); analysis.syllable.assign(1000, 255);
    const auto before = srt.lines[0].endMs;
    check(!Align(srt, analysis).changed && srt.lines[0].endMs == before, "audio heuristics preserve supplied timing");
    check(ParseTimedText(u"not a subtitle"_q).lines.empty(), "malformed input rejected");
    check(ParseTimedText(u"[offset:-9223372036854775808]\n[00:01.000]Hello"_q).lines[0].ms == 1000, "overflowing LRC offset ignored");
    check(ParseTimedText(u"999999999999999999999:00:00.000 --> 999999999999999999999:00:01.000\nHello"_q).lines.empty(), "overflowing subtitle timestamp rejected");
    Ayu::Ui::Net::fixtureEnabled = true;
    check(FetchSync(u"Artist"_q, u"Song"_q, 122).synced, "encoder padding preserves available timed lyrics");
    check(!FetchSync(u"Other artist"_q, u"Song"_q, 119).hasData, "same duration cannot override wrong artist");
    check(!FetchSync(u"Artist"_q, u"Song"_q, 130).hasData, "different recording duration rejected");
    std::cout << "Karaoke timing regression checks passed\n";
}
