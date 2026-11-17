"""Local forced alignment; requires stable-ts, Whisper, PyTorch and FFmpeg.

Usage: python align.py audio lyrics.txt aligned.lrc /path/to/model.pt
The caller supplies a local model. Audio is never uploaded.
"""
import math
import os
from pathlib import Path
import sys


def timestamp(seconds):
    if not math.isfinite(seconds) or seconds < 0:
        raise ValueError("Invalid timestamp")
    millis = round(seconds * 1000)
    return f"{millis // 60000:02}:{millis // 1000 % 60:02}.{millis % 1000:03}"


def render(result, original):
    segments = result.to_dict()["segments"]
    if len(segments) != len(original):
        raise ValueError("Alignment changed line segmentation")
    output = []
    previous = 0.0
    for segment, text in zip(segments, original):
        words = segment["words"]
        if not words or "".join(w["word"] for w in words).split() != text.split():
            raise ValueError("Alignment changed the supplied words")
        line = f"[{timestamp(words[0]['start'])}]"
        for word in words:
            start, end = word["start"], word["end"]
            if not (math.isfinite(start) and math.isfinite(end) and previous <= start < end):
                raise ValueError("Non-monotonic word timing")
            line += f"<{timestamp(start)}>{word['word']}<{timestamp(end)}>"
            previous = end
        output.append(line)
    return "\n".join(output) + "\n"


def main():
    audio_path, text_path, output_path, model_path = sys.argv[1:]
    if not Path(model_path).is_file():
        raise ValueError("A local Whisper checkpoint is required")
    import stable_whisper
    import whisper

    lines = [s.strip() for s in Path(text_path).read_text(encoding="utf-8-sig").splitlines() if s.strip()]
    audio = whisper.load_audio(audio_path)
    model = stable_whisper.load_model(model_path, device=os.getenv("WHITEGRAM_KARAOKE_DEVICE", "cpu"))
    language = os.getenv("WHITEGRAM_KARAOKE_LANGUAGE")
    if not language:
        # Avoid relying only on an instrumental introduction for language detection.
        start = min(len(audio) // 3, 30 * whisper.audio.SAMPLE_RATE)
        mel = whisper.log_mel_spectrogram(whisper.pad_or_trim(audio[start:]), n_mels=model.dims.n_mels).to(model.device)
        _, probabilities = model.detect_language(mel)
        language = max(probabilities, key=probabilities.get)
    result = model.align(audio, "\n".join(lines), language=language, original_split=True, verbose=None)
    if result is None:
        raise ValueError("Alignment failed")
    Path(output_path).write_text(render(result, lines), encoding="utf-8")


if __name__ == "__main__":
    main()
