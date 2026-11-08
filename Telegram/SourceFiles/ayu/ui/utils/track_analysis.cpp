// This file is part of Whitegram for Desktop.
#include "ayu/ui/utils/track_analysis.h"

#include "core/file_location.h"
#include "media/audio/media_audio_ffmpeg_loader.h"

#include <al.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <numeric>

namespace Ayu::Ui::Analysis {
namespace {

constexpr auto kTargetRate = 11025.;
constexpr auto kFftSize = 2048;
constexpr auto kHop = 256;
constexpr auto kMaxSeconds = 15 * 60;
constexpr auto kMelBands = 26;
constexpr auto kMfcc = 13;
constexpr auto kSegment = 8;
constexpr auto kNoveltyHalf = 8;
constexpr auto kBands = 12;
constexpr auto kPi = 3.14159265358979323846;

using Vector = std::vector<float>;

struct Pcm {
	Vector samples;
	double rate = 0.;
};

Pcm Decode(const Core::FileLocation &file, const QByteArray &data) {
	auto loader = Media::FFMpegLoader(file, data, bytes::vector());
	if (!loader.open(0)) {
		return {};
	}
	const auto frequency = loader.samplesFrequency();
	const auto format = loader.format();
	const auto stereo = (format == AL_FORMAT_STEREO8)
		|| (format == AL_FORMAT_STEREO16);
	const auto wide = (format == AL_FORMAT_MONO16)
		|| (format == AL_FORMAT_STEREO16);
	if (frequency <= 0
		|| (!wide && format != AL_FORMAT_MONO8 && format != AL_FORMAT_STEREO8)) {
		return {};
	}
	const auto factor = std::max(1, int(std::lround(frequency / kTargetRate)));
	const auto group = factor * (stereo ? 2 : 1);
	auto result = Pcm{ .rate = double(frequency) / factor };
	const auto limit = std::size_t(result.rate * kMaxSeconds);
	result.samples.reserve(std::min<std::size_t>(
		limit,
		std::size_t(std::max<crl::time>(loader.duration(), 0) * result.rate / 1000 + 1)));
	auto sum = 0.f;
	auto count = 0;
	const auto push = [&](float sample) {
		sum += sample;
		if (++count == group) {
			result.samples.push_back(sum / group);
			sum = 0.f;
			count = 0;
		}
	};
	using Error = Media::AudioPlayerLoader::ReadError;
	while (result.samples.size() < limit) {
		const auto read = loader.readMore();
		if (const auto error = std::get_if<Error>(&read)) {
			if (*error == Error::Retry || *error == Error::RetryNotQueued) {
				continue;
			}
			break;
		}
		const auto span = std::get<bytes::const_span>(read);
		if (wide) {
			const auto values = reinterpret_cast<const int16_t*>(span.data());
			const auto size = span.size() / sizeof(int16_t);
			for (auto i = std::size_t(); i != size; ++i) {
				push(values[i] / 32768.f);
			}
		} else {
			const auto values = reinterpret_cast<const uint8_t*>(span.data());
			for (auto i = std::size_t(); i != std::size_t(span.size()); ++i) {
				push((int(values[i]) - 128) / 128.f);
			}
		}
	}
	return result;
}

class Fft {
public:
	explicit Fft(int size) : _size(size), _reverse(size), _twiddles(size / 2) {
		auto bits = 0;
		while ((1 << bits) < size) {
			++bits;
		}
		for (auto i = 0; i != size; ++i) {
			auto r = 0;
			for (auto b = 0; b != bits; ++b) {
				r |= ((i >> b) & 1) << (bits - 1 - b);
			}
			_reverse[i] = r;
		}
		for (auto i = 0; i != size / 2; ++i) {
			_twiddles[i] = std::polar(1.f, float(-2. * kPi * i / size));
		}
	}

	void run(std::vector<std::complex<float>> &data) const {
		for (auto i = 0; i != _size; ++i) {
			if (i < _reverse[i]) {
				std::swap(data[i], data[_reverse[i]]);
			}
		}
		for (auto length = 2; length <= _size; length <<= 1) {
			const auto half = length / 2;
			const auto step = _size / length;
			for (auto start = 0; start < _size; start += length) {
				for (auto k = 0; k != half; ++k) {
					const auto t = _twiddles[k * step] * data[start + k + half];
					data[start + k + half] = data[start + k] - t;
					data[start + k] += t;
				}
			}
		}
	}

private:
	int _size = 0;
	std::vector<int> _reverse;
	std::vector<std::complex<float>> _twiddles;

};

struct Frames {
	int count = 0;
	double fps = 0.;
	Vector bass;
	Vector rms;
	Vector vocal;
	Vector onset;
	Vector voiceFlux;
	std::vector<std::array<float, 12>> chroma;
	std::vector<std::array<float, kMfcc>> mfcc;
	std::vector<std::array<float, kBands>> bands;
};

Frames ComputeFrames(const Pcm &pcm) {
	auto result = Frames();
	const auto total = int(pcm.samples.size());
	if (total < kFftSize * 4) {
		return result;
	}
	result.count = 1 + (total - kFftSize) / kHop;
	result.fps = pcm.rate / kHop;
	result.bass.resize(result.count);
	result.rms.resize(result.count);
	result.vocal.resize(result.count);
	result.onset.resize(result.count);
	result.voiceFlux.resize(result.count);
	result.chroma.resize(result.count);
	result.mfcc.resize(result.count);
	result.bands.resize(result.count);

	const auto bins = kFftSize / 2;
	const auto binHz = pcm.rate / kFftSize;
	auto window = Vector(kFftSize);
	for (auto i = 0; i != kFftSize; ++i) {
		window[i] = float(0.5 - 0.5 * std::cos(2. * kPi * i / (kFftSize - 1)));
	}
	const auto binOf = [&](double hz) {
		return std::clamp(int(std::lround(hz / binHz)), 1, bins - 1);
	};
	const auto bassFrom = binOf(65.), bassTill = binOf(140.);
	auto bandEdges = std::array<int, kBands + 1>();
	for (auto b = 0; b <= kBands; ++b) {
		bandEdges[b] = binOf(45. * std::pow(6000. / 45., double(b) / kBands));
	}
	const auto vocalFrom = binOf(300.), vocalTill = binOf(4000.);
	const auto voiceFrom = binOf(300.), voiceTill = binOf(3400.);

	auto pitchClass = std::vector<int>(bins, -1);
	for (auto k = 1; k != bins; ++k) {
		const auto hz = k * binHz;
		if (hz >= 55. && hz <= 2000.) {
			const auto midi = int(std::lround(12. * std::log2(hz / 440.) + 69.));
			pitchClass[k] = ((midi % 12) + 12) % 12;
		}
	}

	const auto mel = [](double hz) { return 2595. * std::log10(1. + hz / 700.); };
	const auto hzOf = [](double m) { return 700. * (std::pow(10., m / 2595.) - 1.); };
	const auto melLow = mel(40.);
	const auto melHigh = mel(std::min(5000., pcm.rate / 2. - binHz));
	auto edges = std::vector<double>(kMelBands + 2);
	for (auto i = 0; i != kMelBands + 2; ++i) {
		edges[i] = hzOf(melLow + (melHigh - melLow) * i / (kMelBands + 1)) / binHz;
	}
	auto dct = std::vector<std::array<float, kMelBands>>(kMfcc);
	for (auto c = 0; c != kMfcc; ++c) {
		for (auto m = 0; m != kMelBands; ++m) {
			dct[c][m] = float(std::cos(kPi * (c + 1) * (m + 0.5) / kMelBands));
		}
	}

	const auto fft = Fft(kFftSize);
	auto buffer = std::vector<std::complex<float>>(kFftSize);
	auto power = Vector(bins);
	auto logMag = Vector(bins), previous = Vector(bins);
	for (auto f = 0; f != result.count; ++f) {
		const auto offset = f * kHop;
		auto energy = 0.;
		for (auto i = 0; i != kFftSize; ++i) {
			const auto sample = pcm.samples[offset + i];
			energy += sample * sample;
			buffer[i] = { sample * window[i], 0.f };
		}
		fft.run(buffer);
		auto totalPower = 0.;
		auto chroma = std::array<float, 12>{};
		auto flux = 0.f;
		auto voiceFlux = 0.f;
		for (auto k = 1; k != bins; ++k) {
			power[k] = std::norm(buffer[k]);
			totalPower += power[k];
			logMag[k] = std::log1p(10.f * std::sqrt(power[k]));
			const auto rise = std::max(0.f, logMag[k] - previous[k]);
			flux += rise;
			if (k >= voiceFrom && k <= voiceTill) {
				voiceFlux += rise;
			}
			if (pitchClass[k] >= 0) {
				chroma[pitchClass[k]] += std::sqrt(power[k]);
			}
		}
		std::swap(logMag, previous);
		auto bass = 0., vocal = 0.;
		for (auto k = bassFrom; k <= bassTill; ++k) {
			bass += power[k];
		}
		for (auto k = vocalFrom; k <= vocalTill; ++k) {
			vocal += power[k];
		}
		const auto chromaMax = *std::max_element(chroma.begin(), chroma.end());
		if (chromaMax > 0.f) {
			for (auto &value : chroma) {
				value /= chromaMax;
			}
		}
		auto bands = std::array<float, kMelBands>{};
		for (auto m = 0; m != kMelBands; ++m) {
			const auto left = edges[m], center = edges[m + 1], right = edges[m + 2];
			auto sum = 0.;
			for (auto k = std::max(1, int(left)); k <= std::min(bins - 1, int(right) + 1); ++k) {
				const auto weight = (k < center)
					? (k - left) / std::max(1e-6, center - left)
					: (right - k) / std::max(1e-6, right - center);
				if (weight > 0.) {
					sum += weight * power[k];
				}
			}
			bands[m] = float(std::log(sum + 1e-10));
		}
		auto coefficients = std::array<float, kMfcc>{};
		for (auto c = 0; c != kMfcc; ++c) {
			auto sum = 0.f;
			for (auto m = 0; m != kMelBands; ++m) {
				sum += dct[c][m] * bands[m];
			}
			coefficients[c] = sum;
		}
		result.bass[f] = float(10. * std::log10(bass + 1e-10));
		result.rms[f] = float(std::sqrt(energy / kFftSize));
		result.vocal[f] = float((totalPower > 0.) ? (vocal / totalPower) : 0.);
		result.onset[f] = (f > 0) ? flux : 0.f;
		result.voiceFlux[f] = (f > 0) ? voiceFlux : 0.f;
		result.chroma[f] = chroma;
		result.mfcc[f] = coefficients;
		for (auto b = 0; b != kBands; ++b) {
			auto sum = 0.;
			for (auto k = bandEdges[b]; k <= std::max(bandEdges[b], bandEdges[b + 1] - 1); ++k) {
				sum += power[k];
			}
			result.bands[f][b] = float(10. * std::log10(sum + 1e-10));
		}
	}
	return result;
}

float Percentile(Vector values, double fraction) {
	if (values.empty()) {
		return 0.f;
	}
	const auto index = std::clamp(
		std::size_t(fraction * (values.size() - 1)),
		std::size_t(0),
		values.size() - 1);
	std::nth_element(values.begin(), values.begin() + index, values.end());
	return values[index];
}

float Median(const Vector &values, int from, int till) {
	from = std::max(from, 0);
	till = std::min(till, int(values.size()));
	if (till <= from) {
		return 0.f;
	}
	return Percentile(Vector(values.begin() + from, values.begin() + till), 0.5);
}

Vector Ranks(const Vector &values) {
	const auto n = int(values.size());
	auto order = std::vector<int>(n);
	std::iota(order.begin(), order.end(), 0);
	std::sort(order.begin(), order.end(), [&](int a, int b) {
		return values[a] < values[b];
	});
	auto result = Vector(n);
	for (auto i = 0; i < n;) {
		auto j = i;
		while (j + 1 < n && values[order[j + 1]] == values[order[i]]) {
			++j;
		}
		const auto rank = (n > 1) ? float((i + j) / 2.) / (n - 1) : 0.f;
		for (auto k = i; k <= j; ++k) {
			result[order[k]] = rank;
		}
		i = j + 1;
	}
	return result;
}

float EntropyWeight(const Vector &values) {
	if (values.size() < 2) {
		return 0.05f;
	}
	const auto [low, high] = std::minmax_element(values.begin(), values.end());
	if (*high - *low < 1e-6f) {
		return 0.05f;
	}
	constexpr auto kBins = 16;
	auto histogram = std::array<int, kBins>{};
	for (const auto value : values) {
		const auto bin = std::min(kBins - 1, int((value - *low) / (*high - *low) * kBins));
		++histogram[bin];
	}
	auto entropy = 0.;
	for (const auto count : histogram) {
		if (count) {
			const auto p = double(count) / values.size();
			entropy -= p * std::log(p);
		}
	}
	return std::max(0.05f, float(entropy / std::log(double(kBins))));
}

Vector Detrended(const Vector &onset, int radius) {
	auto result = Vector(onset.size());
	auto prefix = std::vector<double>(onset.size() + 1);
	for (auto i = std::size_t(); i != onset.size(); ++i) {
		prefix[i + 1] = prefix[i] + onset[i];
	}
	for (auto i = 0; i != int(onset.size()); ++i) {
		const auto from = std::max(0, i - radius);
		const auto till = std::min(int(onset.size()), i + radius + 1);
		const auto mean = (prefix[till] - prefix[from]) / (till - from);
		result[i] = std::max(0.f, float(onset[i] - mean));
	}
	auto sum = 0., squares = 0.;
	for (const auto value : result) {
		sum += value;
		squares += value * value;
	}
	const auto deviation = std::sqrt(std::max(1e-12, squares / result.size()
		- (sum / result.size()) * (sum / result.size())));
	for (auto &value : result) {
		value = float(value / deviation);
	}
	return result;
}

std::vector<int> TrackBeats(const Vector &onset, double fps, double &bpm) {
	const auto n = int(onset.size());
	const auto lagMin = std::max(2, int(fps * 60. / 200.));
	const auto lagMax = std::min(n / 4, int(fps * 60. / 55.));
	auto bestLag = 0;
	auto bestValue = -1.;
	for (auto lag = lagMin; lag <= lagMax; ++lag) {
		auto sum = 0.;
		for (auto i = 0; i + lag < n; ++i) {
			sum += onset[i] * onset[i + lag];
		}
		sum /= (n - lag);
		const auto tempo = 60. * fps / lag;
		const auto octave = std::log2(tempo / 120.);
		const auto weighted = sum * std::exp(-0.5 * octave * octave);
		if (weighted > bestValue) {
			bestValue = weighted;
			bestLag = lag;
		}
	}
	if (!bestLag) {
		return {};
	}
	bpm = 60. * fps / bestLag;
	const auto period = double(bestLag);
	constexpr auto kTightness = 100.;
	auto score = std::vector<double>(n);
	auto back = std::vector<int>(n, -1);
	for (auto t = 0; t != n; ++t) {
		auto best = 0.;
		auto from = -1;
		const auto earliest = std::max(0, t - int(2 * period));
		const auto latest = t - int(period / 2);
		for (auto prev = earliest; prev <= latest; ++prev) {
			const auto ratio = std::log((t - prev) / period);
			const auto value = score[prev] - kTightness * ratio * ratio;
			if (from < 0 || value > best) {
				best = value;
				from = prev;
			}
		}
		score[t] = onset[t] + ((from >= 0) ? best : 0.);
		back[t] = from;
	}
	auto last = std::max(0, n - int(period));
	for (auto t = last; t != n; ++t) {
		if (score[t] > score[last]) {
			last = t;
		}
	}
	auto beats = std::vector<int>();
	for (auto t = last; t >= 0; t = back[t]) {
		beats.push_back(t);
		if (back[t] < 0) {
			break;
		}
	}
	std::reverse(beats.begin(), beats.end());
	return beats;
}

struct Signals {
	Vector repeat;
	Vector novelty;
	Vector loudJump;
	Vector vocalJump;
	Vector bassLevel;
	Vector prior;
	Vector lyrics;
};

void ZScore(Vector &values) {
	if (values.empty()) {
		return;
	}
	const auto mean = std::accumulate(values.begin(), values.end(), 0.) / values.size();
	auto squares = 0.;
	for (const auto value : values) {
		squares += (value - mean) * (value - mean);
	}
	const auto deviation = std::sqrt(std::max(1e-12, squares / values.size()));
	for (auto &value : values) {
		value = float((value - mean) / deviation);
	}
}

void Normalize(
		const Vector &source,
		std::vector<std::uint8_t> &target,
		double low,
		double high) {
	const auto lo = Percentile(source, low);
	const auto hi = Percentile(source, high);
	const auto span = std::max(1e-6f, hi - lo);
	target.resize(source.size());
	for (auto i = std::size_t(); i != source.size(); ++i) {
		target[i] = std::uint8_t(std::clamp((source[i] - lo) / span, 0.f, 1.f) * 255.f);
	}
}

void BuildPulse(const Frames &frames, Result &result) {
	auto level = Vector(frames.count);
	const auto lo = Percentile(frames.bass, 0.10);
	const auto hi = Percentile(frames.bass, 0.98);
	for (auto i = 0; i != frames.count; ++i) {
		level[i] = std::clamp((frames.bass[i] - lo) / std::max(1e-6f, hi - lo), 0.f, 1.f);
	}
	auto hits = Vector(frames.count);
	constexpr auto kHistory = 10;
	for (auto i = 1; i != frames.count; ++i) {
		auto sum = 0.f;
		const auto from = std::max(0, i - kHistory);
		for (auto k = from; k != i; ++k) {
			sum += level[k];
		}
		hits[i] = std::max(0.f, level[i] - sum / (i - from));
	}
	const auto hitTop = std::max(1e-6f, Percentile(hits, 0.98));
	auto envelope = 0.f;
	auto pulse = Vector(frames.count);
	for (auto i = 0; i != frames.count; ++i) {
		const auto value = std::clamp(
			0.6f * std::min(1.f, hits[i] / hitTop) + 0.4f * level[i] * level[i],
			0.f,
			1.f);
		envelope = std::max(value, envelope * 0.86f);
		pulse[i] = envelope;
	}
	Normalize(pulse, result.pulse, 0., 1.);
	Normalize(frames.rms, result.energy, 0.10, 0.98);
}

} // namespace

Vector BoxSmooth(const Vector &values, int radius) {
	const auto n = int(values.size());
	auto prefix = std::vector<double>(n + 1, 0.);
	for (auto i = 0; i != n; ++i) {
		prefix[i + 1] = prefix[i] + values[i];
	}
	auto result = Vector(n);
	for (auto i = 0; i != n; ++i) {
		const auto from = std::max(0, i - radius);
		const auto till = std::min(n, i + radius + 1);
		result[i] = float((prefix[till] - prefix[from]) / std::max(1, till - from));
	}
	return result;
}

std::vector<std::uint8_t> Quantize(const Vector &values, float lo, float hi) {
	auto result = std::vector<std::uint8_t>(values.size());
	const auto span = std::max(1e-6f, hi - lo);
	for (auto i = 0; i != int(values.size()); ++i) {
		result[i] = std::uint8_t(std::clamp((values[i] - lo) / span, 0.f, 1.f) * 255.f);
	}
	return result;
}

void BuildVoice(const Frames &frames, Result &result) {
	const auto n = frames.count;
	const auto fps = frames.fps;
	if (n < 8) {
		return;
	}
	// WHY: a voice lifts the 300-3400 Hz share of the mix and keeps re-attacking it every syllable,
	// while beats and pads either stay below that share or attack outside the band.
	const auto share = BoxSmooth(frames.vocal, std::max(1, int(fps * 0.12)));
	auto loudness = Vector(n);
	for (auto i = 0; i != n; ++i) {
		loudness[i] = 20.f * std::log10(frames.rms[i] + 1e-6f);
	}
	const auto silence = Percentile(loudness, 0.08) + 6.f;
	auto activity = BoxSmooth(frames.voiceFlux, std::max(1, int(fps * 0.2)));
	const auto shareNorm = Quantize(share, Percentile(share, 0.1), Percentile(share, 0.92));
	const auto activityNorm = Quantize(activity, Percentile(activity, 0.1), Percentile(activity, 0.92));
	auto voice = Vector(n);
	for (auto i = 0; i != n; ++i) {
		const auto audible = (loudness[i] > silence) ? 1.f : 0.f;
		voice[i] = audible * (0.55f * shareNorm[i] + 0.45f * activityNorm[i]) / 255.f;
	}
	voice = BoxSmooth(voice, std::max(1, int(fps * 0.08)));
	result.voice = Quantize(voice, 0.f, 1.f);

	const auto local = BoxSmooth(frames.voiceFlux, std::max(1, int(fps * 0.5)));
	auto peaks = Vector(n);
	for (auto i = 0; i != n; ++i) {
		peaks[i] = std::max(0.f, frames.voiceFlux[i] - local[i]);
	}
	result.syllable = Quantize(peaks, 0.f, std::max(1e-6f, Percentile(peaks, 0.985)));
}

float Result::pulseAt(std::int64_t ms) const {
	if (pulse.empty() || frameMs <= 0.) {
		return 0.f;
	}
	const auto index = std::clamp(int(ms / frameMs), 0, int(pulse.size()) - 1);
	return pulse[index] / 255.f;
}

float Result::spectrumAt(std::int64_t ms, int band) const {
	if (!bands || spectrum.empty() || frameMs <= 0.) {
		return 0.f;
	}
	const auto frames = int(spectrum.size()) / bands;
	const auto index = std::clamp(int(ms / frameMs), 0, frames - 1);
	return spectrum[index * bands + std::clamp(band, 0, bands - 1)] / 255.f;
}

float Result::energyAt(std::int64_t ms) const {
	if (energy.empty() || frameMs <= 0.) {
		return 0.f;
	}
	const auto index = std::clamp(int(ms / frameMs), 0, int(energy.size()) - 1);
	return energy[index] / 255.f;
}

bool Result::inChorus(std::int64_t ms) const {
	return std::any_of(chorus.begin(), chorus.end(), [&](const Section &s) {
		return ms >= s.startMs && ms < s.endMs;
	});
}

Result Analyze(
		const Core::FileLocation &file,
		const QByteArray &data,
		const std::vector<Section> &lyricBlocks) {
	auto result = Result();
	auto lyricRepeats = std::vector<std::int64_t>();
	for (const auto &block : lyricBlocks) {
		lyricRepeats.push_back(block.startMs);
	}
	const auto pcm = Decode(file, data);
	if (pcm.samples.empty()) {
		return result;
	}
	const auto frames = ComputeFrames(pcm);
	if (!frames.count) {
		return result;
	}
	result.ok = true;
	result.frameMs = 1000. / frames.fps;
	result.durationMs = std::int64_t(pcm.samples.size() * 1000. / pcm.rate);
	BuildPulse(frames, result);
	BuildVoice(frames, result);
	result.bands = kBands;
	result.spectrum.resize(std::size_t(frames.count) * kBands);
	for (auto b = 0; b != kBands; ++b) {
		auto column = Vector(frames.count);
		for (auto f = 0; f != frames.count; ++f) {
			column[f] = frames.bands[f][b];
		}
		const auto lo = Percentile(column, 0.15);
		const auto hi = Percentile(column, 0.985);
		const auto span = std::max(1e-6f, hi - lo);
		for (auto f = 0; f != frames.count; ++f) {
			result.spectrum[std::size_t(f) * kBands + b] = std::uint8_t(
				std::clamp((column[f] - lo) / span, 0.f, 1.f) * 255.f);
		}
	}
	if (!lyricBlocks.empty()) {
		result.chorus = lyricBlocks;
		result.confidence = 0.9;
	}

	const auto onset = Detrended(frames.onset, int(frames.fps));
	auto beatFrames = TrackBeats(onset, frames.fps, result.bpm);
	if (beatFrames.size() < 4 * kSegment || !lyricBlocks.empty()) {
		return result;
	}
	for (const auto frame : beatFrames) {
		result.beats.push_back(std::int64_t(frame * result.frameMs));
	}

	const auto n = int(beatFrames.size());
	constexpr auto kDims = 12 + kMfcc;
	auto features = std::vector<std::array<float, kDims>>(n);
	auto rms = Vector(n), vocal = Vector(n), bass = Vector(n), strength = Vector(n);
	for (auto b = 0; b != n; ++b) {
		const auto from = beatFrames[b];
		const auto till = (b + 1 < n) ? beatFrames[b + 1] : std::min(frames.count, from + 1 + (from - beatFrames[std::max(0, b - 1)]));
		const auto count = std::max(1, till - from);
		auto &vector = features[b];
		vector.fill(0.f);
		for (auto f = from; f < from + count && f < frames.count; ++f) {
			for (auto c = 0; c != 12; ++c) {
				vector[c] += frames.chroma[f][c] / count;
			}
			for (auto c = 0; c != kMfcc; ++c) {
				vector[12 + c] += frames.mfcc[f][c] / count;
			}
			rms[b] += frames.rms[f] / count;
			vocal[b] += frames.vocal[f] / count;
			bass[b] += frames.bass[f] / count;
		}
		strength[b] = onset[from];
	}
	for (auto d = 0; d != kDims; ++d) {
		auto column = Vector(n);
		for (auto b = 0; b != n; ++b) {
			column[b] = features[b][d];
		}
		ZScore(column);
		for (auto b = 0; b != n; ++b) {
			features[b][d] = column[b];
		}
	}
	ZScore(rms);
	ZScore(vocal);
	ZScore(bass);

	auto norms = Vector(n);
	for (auto b = 0; b != n; ++b) {
		auto sum = 0.f;
		for (const auto value : features[b]) {
			sum += value * value;
		}
		norms[b] = std::sqrt(std::max(sum, 1e-12f));
	}
	auto similarity = Vector(std::size_t(n) * n);
	const auto S = [&](int i, int j) -> float& {
		return similarity[std::size_t(i) * n + j];
	};
	for (auto i = 0; i != n; ++i) {
		for (auto j = i; j != n; ++j) {
			auto dot = 0.f;
			for (auto d = 0; d != kDims; ++d) {
				dot += features[i][d] * features[j][d];
			}
			const auto value = 0.5f * (1.f + dot / (norms[i] * norms[j]));
			S(i, j) = S(j, i) = value;
		}
	}

	const auto segments = n - kSegment + 1;
	auto segment = Vector(std::size_t(segments) * segments, 0.f);
	const auto D = [&](int i, int j) -> float& {
		return segment[std::size_t(i) * segments + j];
	};
	auto pool = Vector();
	pool.reserve(std::size_t(segments) * segments / 2);
	for (auto i = 0; i != segments; ++i) {
		for (auto j = i + kSegment; j < segments; ++j) {
			auto sum = 0.f;
			for (auto m = 0; m != kSegment; ++m) {
				sum += S(i + m, j + m);
			}
			D(i, j) = D(j, i) = sum / kSegment;
			pool.push_back(sum / kSegment);
		}
	}
	const auto threshold = Percentile(pool, 0.90);
	const auto isMatch = [&](int i, int j) {
		if (std::abs(i - j) < kSegment || D(i, j) <= threshold) {
			return false;
		}
		const auto left = (j > 0) ? D(i, j - 1) : 0.f;
		const auto right = (j + 1 < segments) ? D(i, j + 1) : 0.f;
		return D(i, j) >= left && D(i, j) >= right;
	};

	auto signals = Signals();
	signals.repeat.resize(n);
	signals.novelty.resize(n);
	signals.loudJump.resize(n);
	signals.vocalJump.resize(n);
	signals.bassLevel.resize(n);
	signals.prior.resize(n);
	signals.lyrics.resize(n);
	const auto sigma = kNoveltyHalf / 2.;
	for (auto i = 0; i != n; ++i) {
		if (i < segments) {
			for (auto j = 0; j != segments; ++j) {
				if (isMatch(i, j)) {
					signals.repeat[i] += 1.f;
				}
			}
		}
		auto novelty = 0.;
		for (auto a = -kNoveltyHalf; a != kNoveltyHalf; ++a) {
			for (auto b = -kNoveltyHalf; b != kNoveltyHalf; ++b) {
				const auto x = i + a, y = i + b;
				if (x < 0 || y < 0 || x >= n || y >= n) {
					continue;
				}
				const auto sign = ((a < 0) == (b < 0)) ? 1. : -1.;
				const auto taper = std::exp(-(a + 0.5) * (a + 0.5) / (2 * sigma * sigma))
					* std::exp(-(b + 0.5) * (b + 0.5) / (2 * sigma * sigma));
				novelty += sign * taper * S(x, y);
			}
		}
		signals.novelty[i] = float(std::max(0., novelty));
		signals.loudJump[i] = Median(rms, i, i + kSegment) - Median(rms, i - kSegment, i);
		signals.vocalJump[i] = Median(vocal, i, i + kSegment) - Median(vocal, i - kSegment, i);
		signals.bassLevel[i] = Median(bass, i, i + kSegment);
		const auto x = double(i) / n;
		signals.prior[i] = float((x < 0.08)
			? (x / 0.08) * 0.6
			: (x > 0.85)
			? (1. - (x - 0.85) / 0.15 * 0.6)
			: 1.);
	}
	const auto beatMs = [&](int b) { return result.beats[std::clamp(b, 0, n - 1)]; };
	for (const auto ms : lyricRepeats) {
		const auto it = std::upper_bound(result.beats.begin(), result.beats.end(), ms);
		const auto b = std::max(0, int(it - result.beats.begin()) - 1);
		for (auto k = std::max(0, b - 1); k <= std::min(n - 1, b + 1); ++k) {
			signals.lyrics[k] = 1.f;
		}
	}

	struct Weighted {
		const Vector *values;
		float weight;
		bool ranked;
	};
	const auto hasLyrics = !lyricRepeats.empty();
	const auto parts = std::vector<Weighted>{
		{ &signals.repeat, EntropyWeight(signals.repeat), true },
		{ &signals.novelty, EntropyWeight(signals.novelty), true },
		{ &signals.loudJump, EntropyWeight(signals.loudJump), true },
		{ &signals.vocalJump, EntropyWeight(signals.vocalJump), true },
		{ &signals.bassLevel, EntropyWeight(signals.bassLevel), true },
		{ &signals.prior, 0.5f, false },
		{ &signals.lyrics, hasLyrics ? 1.f : 0.f, false },
	};
	auto score = Vector(n, 0.f);
	auto totalWeight = 0.f;
	for (const auto &part : parts) {
		if (part.weight <= 0.f) {
			continue;
		}
		const auto values = part.ranked ? Ranks(*part.values) : *part.values;
		for (auto i = 0; i != n; ++i) {
			score[i] += part.weight * values[i];
		}
		totalWeight += part.weight;
	}
	for (auto &value : score) {
		value /= totalWeight;
	}

	auto candidates = std::vector<int>();
	for (auto i = 0; i != n; ++i) {
		auto peak = true;
		for (auto k = std::max(0, i - kSegment); k <= std::min(n - 1, i + kSegment); ++k) {
			if (score[k] > score[i]) {
				peak = false;
				break;
			}
		}
		if (peak) {
			candidates.push_back(i);
		}
	}
	std::sort(candidates.begin(), candidates.end(), [&](int a, int b) {
		return score[a] > score[b];
	});
	if (candidates.empty()) {
		return result;
	}
	auto passing = std::vector<int>();
	for (const auto c : candidates) {
		if (c < segments && signals.repeat[c] >= 1.f) {
			passing.push_back(c);
		}
	}
	auto best = -1;
	if (!passing.empty()) {
		best = passing.front();
		result.confidence = (passing.size() > 1 && score[best] > 0.f)
			? std::clamp((score[best] - score[passing[1]]) / score[best] * 4.f, 0.f, 1.f)
			: 0.6;
	} else {
		const auto most = std::max_element(signals.repeat.begin(), signals.repeat.end());
		if (*most >= 1.f) {
			best = int(most - signals.repeat.begin());
			result.confidence = 0.3;
		} else {
			auto loudest = Vector(n);
			const auto loud = Ranks(signals.loudJump);
			const auto nov = Ranks(signals.novelty);
			const auto low = Ranks(signals.bassLevel);
			for (auto i = 0; i != n; ++i) {
				loudest[i] = loud[i] + nov[i] + low[i];
			}
			best = int(std::max_element(loudest.begin(), loudest.end()) - loudest.begin());
			result.confidence = 0.1;
		}
	}

	auto starts = std::vector<int>{ best };
	if (best < segments) {
		for (auto j = 0; j != segments; ++j) {
			if (isMatch(best, j)) {
				starts.push_back(j);
			}
		}
	}
	std::sort(starts.begin(), starts.end());
	auto merged = std::vector<int>();
	for (const auto s : starts) {
		if (merged.empty() || s - merged.back() >= kSegment * 2) {
			merged.push_back(s);
		}
	}
	const auto noveltyRanks = Ranks(signals.novelty);
	for (const auto start : merged) {
		auto snapped = start;
		for (auto k = std::max(0, start - 2); k <= std::min(n - 1, start + 2); ++k) {
			if (strength[k] > strength[snapped]) {
				snapped = k;
			}
		}
		auto startMs = beatMs(snapped);
		auto nearest = std::int64_t(-1);
		for (const auto ms : lyricRepeats) {
			if (std::abs(ms - startMs) <= 1500
				&& (nearest < 0 || std::abs(ms - startMs) < std::abs(nearest - startMs))) {
				nearest = ms;
			}
		}
		if (nearest >= 0) {
			startMs = nearest;
		}
		auto end = std::min(n - 1, snapped + 4 * kSegment);
		for (auto k = snapped + kSegment; k < std::min(n, snapped + 4 * kSegment); ++k) {
			if (noveltyRanks[k] >= 0.7f
				&& (k + 1 >= n || signals.novelty[k] >= signals.novelty[k + 1])
				&& (k == 0 || signals.novelty[k] >= signals.novelty[k - 1])) {
				end = k;
				break;
			}
		}
		const auto endMs = (end + 1 < n) ? beatMs(end) : result.durationMs;
		if (endMs > startMs) {
			result.chorus.push_back({ startMs, endMs });
		}
	}
	return result;
}

} // namespace Ayu::Ui::Analysis
