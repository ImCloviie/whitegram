#!/usr/bin/env python3
"""Whitegram pre-beta version encoding and project file updates."""

from __future__ import annotations

import json
import re
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
VERSION_FILE = ROOT / "Telegram/build/version"
VERSION_H = ROOT / "Telegram/SourceFiles/core/version.h"
CLIENT_INFO = ROOT / "backend/data/client-info.json"
CURRENT_JSON = ROOT / "backend/data/updates/current.json"

LEGACY_1_0_BASE = 7000008
APP_VERSION_BASE = 7000000
BETA_LABEL = "pre-beta"


@dataclass(frozen=True)
class PreBetaVersion:
	major: int
	minor: int
	patch: int

	def __str__(self) -> str:
		return f"{self.major}.{self.minor}.{self.patch}"

	@classmethod
	def parse(cls, text: str) -> PreBetaVersion:
		parts = text.strip().split(".")
		if len(parts) != 3:
			raise ValueError(f"Expected major.minor.patch, got {text!r}")
		major, minor, patch = (int(part) for part in parts)
		for name, value in (
			("major", major),
			("minor", minor),
			("patch", patch),
		):
			if value < 0:
				raise ValueError(f"{name} must be >= 0")
		return cls(major, minor, patch)


def prebeta_to_app_version(version: PreBetaVersion) -> int:
	if version.major == 1 and version.minor == 0:
		return LEGACY_1_0_BASE + version.patch
	return (
		APP_VERSION_BASE
		+ version.major * 10000
		+ version.minor * 100
		+ version.patch
	)


def app_version_to_prebeta(app_version: int) -> PreBetaVersion:
	if LEGACY_1_0_BASE <= app_version < 7010000:
		return PreBetaVersion(1, 0, app_version - LEGACY_1_0_BASE)
	offset = app_version - APP_VERSION_BASE
	if offset < 0:
		raise ValueError(f"Unsupported AppVersion {app_version}")
	return PreBetaVersion(
		offset // 10000,
		(offset % 10000) // 100,
		offset % 100,
	)


def bump(version: PreBetaVersion, part: str, amount: int = 1) -> PreBetaVersion:
	if amount < 1:
		raise ValueError("amount must be >= 1")
	if part == "major":
		return PreBetaVersion(version.major + amount, 0, 0)
	if part == "minor":
		return PreBetaVersion(version.major, version.minor + amount, 0)
	if part == "patch":
		return PreBetaVersion(
			version.major,
			version.minor,
			version.patch + amount,
		)
	raise ValueError(f"Unknown bump part {part!r}, use major/minor/patch")


def _is_app_version_line(line: str) -> bool:
	parts = line.split()
	return len(parts) >= 2 and parts[0] == "AppVersion"


def read_app_version() -> int:
	text = VERSION_FILE.read_text(encoding="utf-8")
	for line in text.splitlines():
		if _is_app_version_line(line):
			return int(line.split()[1])
	raise RuntimeError(f"AppVersion not found in {VERSION_FILE}")


def read_prebeta_version() -> PreBetaVersion:
	return app_version_to_prebeta(read_app_version())


def display_version_text(prebeta: PreBetaVersion, *, russian: bool = True) -> str:
	if russian:
		return f"Версия {BETA_LABEL} {prebeta}"
	return f"Version {BETA_LABEL} {prebeta}"


def write_app_version(app_version: int) -> None:
	text = VERSION_FILE.read_text(encoding="utf-8")
	lines = []
	updated = False
	for line in text.splitlines():
		if _is_app_version_line(line):
			lines.append(f"AppVersion         {app_version}")
			updated = True
		else:
			lines.append(line)
	if not updated:
		raise RuntimeError(f"AppVersion not found in {VERSION_FILE}")
	VERSION_FILE.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")

	header = VERSION_H.read_text(encoding="utf-8")
	header, count = re.subn(
		r"constexpr auto AppVersion = \d+;",
		f"constexpr auto AppVersion = {app_version};",
		header,
		count=1,
	)
	if count != 1:
		raise RuntimeError(f"Failed to update AppVersion in {VERSION_H}")
	VERSION_H.write_text(header, encoding="utf-8", newline="\n")


def write_client_info(prebeta: PreBetaVersion) -> None:
	payload = {}
	if CLIENT_INFO.exists():
		try:
			payload = json.loads(CLIENT_INFO.read_text(encoding="utf-8"))
		except json.JSONDecodeError:
			payload = {}
	payload["displayVersion"] = display_version_text(prebeta)
	payload["betaVersion"] = str(prebeta)
	payload["betaLabel"] = BETA_LABEL
	CLIENT_INFO.write_text(
		json.dumps(payload, indent=2, ensure_ascii=False) + "\n",
		encoding="utf-8",
		newline="\n",
	)


def apply_prebeta_version(prebeta: PreBetaVersion) -> int:
	app_version = prebeta_to_app_version(prebeta)
	write_app_version(app_version)
	write_client_info(prebeta)
	return app_version


def update_manifest(app_version: int, pack_name: str) -> None:
	data = json.loads(CURRENT_JSON.read_text(encoding="utf-8"))
	link = f"/updates/{pack_name}"
	for platform in ("win64", "win", "winarm"):
		data.setdefault(platform, {}).setdefault("stable", {})
		data[platform]["stable"]["released"] = app_version
		data[platform]["stable"]["link"] = link
	for platform in ("mac", "linux"):
		data.setdefault(platform, {}).setdefault("stable", {})
		data[platform]["stable"]["released"] = app_version
		data[platform]["stable"]["link"] = (
			f"/updates/Whitegram-{app_version}.zip"
			if platform == "mac"
			else f"/updates/Whitegram-{app_version}.tar.xz"
		)
	CURRENT_JSON.write_text(
		json.dumps(data, indent=2, ensure_ascii=False) + "\n",
		encoding="utf-8",
		newline="\n",
	)
