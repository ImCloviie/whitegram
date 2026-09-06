#!/usr/bin/env python3
"""Whitegram release helper: version files, pack, deploy to VDS."""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import textwrap
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TELEGRAM = ROOT / "Telegram"
OUT = ROOT / "out"
RELEASE = OUT / "Release"
PACK_BAT = TELEGRAM / "build/whitegram_pack_update.bat"
UPDATES_DIR = ROOT / "backend/data/updates"

sys.path.insert(0, str(Path(__file__).resolve().parent))
import version_utils as vu

VS_BUILD_HELP = textwrap.dedent("""
	Build Release in Visual Studio first:

	  1. Open: E:\\WhitegramProject\\out\\Telegram\\Telegram.vcxproj
	     (or the generated solution under E:\\WhitegramProject\\out\\)
	  2. Configuration: Release
	  3. Platform: x64
	  4. Build -> Build Telegram
	  5. Build -> Build Packer  (once, or when signing changed)

	Output must exist:
	  E:\\WhitegramProject\\out\\Release\\Whitegram.exe
	  E:\\WhitegramProject\\out\\Release\\Updater.exe
	  E:\\WhitegramProject\\out\\Release\\Packer.exe
""").strip()


class Step:
	def __init__(self, title: str) -> None:
		self.title = title

	def __enter__(self) -> Step:
		print(f"\n== {self.title} ==")
		return self

	def __exit__(self, exc_type, exc, tb) -> None:
		if exc_type is None:
			print(f"OK: {self.title}")


def run(
	cmd: list[str],
	*,
	cwd: Path | None = None,
	check: bool = True,
) -> subprocess.CompletedProcess[str]:
	display = " ".join(f'"{part}"' if " " in part else part for part in cmd)
	print(f"$ {display}")
	started = time.time()
	result = subprocess.run(
		cmd,
		cwd=cwd,
		text=True,
	)
	elapsed = time.time() - started
	if result.returncode != 0:
		print(f"Failed ({elapsed:.1f}s, code {result.returncode})")
		if check:
			raise subprocess.CalledProcessError(result.returncode, cmd)
	else:
		print(f"Done ({elapsed:.1f}s)")
	return result


def print_status() -> None:
	app_version = vu.read_app_version()
	prebeta = vu.app_version_to_prebeta(app_version)
	pack_name = f"tx64upd{app_version}"
	local_pack = UPDATES_DIR / pack_name
	print("Whitegram release status")
	print(f"  pre-beta:     {prebeta} ({vu.BETA_LABEL})")
	print(f"  AppVersion:   {app_version}")
	print(f"  update pack:  {pack_name}")
	print(f"  pack on disk: {'yes' if local_pack.exists() else 'no'}")
	if local_pack.exists():
		size_mb = local_pack.stat().st_size / (1024 * 1024)
		print(f"  pack size:    {size_mb:.1f} MB")
	print(f"  Whitegram.exe: {'yes' if (RELEASE / 'Whitegram.exe').exists() else 'no'}")
	print(f"  Updater.exe:   {'yes' if (RELEASE / 'Updater.exe').exists() else 'no'}")
	print(f"  Packer.exe:    {'yes' if (RELEASE / 'Packer.exe').exists() else 'no'}")


def verify_release_artifacts() -> None:
	missing = [
		name
		for name in ("Whitegram.exe", "Updater.exe", "Packer.exe")
		if not (RELEASE / name).exists()
	]
	if missing:
		raise RuntimeError(
			f"Missing in {RELEASE}: {', '.join(missing)}\n\n{VS_BUILD_HELP}"
		)
	verify_exe_app_version()


def verify_exe_app_version() -> None:
	expected = vu.read_app_version()
	exe = RELEASE / "Whitegram.exe"
	version_mark = max(
		vu.VERSION_FILE.stat().st_mtime,
		vu.VERSION_H.stat().st_mtime,
	)
	if exe.stat().st_mtime < version_mark:
		raise RuntimeError(
			"Whitegram.exe is older than version files.\n"
			"Rebuild Release in Visual Studio, then run ship again.\n\n"
			f"{VS_BUILD_HELP}"
		)
	needle = expected.to_bytes(4, "little")
	if needle not in exe.read_bytes():
		raise RuntimeError(
			f"Whitegram.exe does not contain AppVersion {expected}.\n"
			"The pack would keep the old client version after update.\n"
			"Rebuild Release in Visual Studio, then run ship again.\n\n"
			f"{VS_BUILD_HELP}"
		)
	print(f"  Whitegram.exe matches AppVersion {expected}")


def apply_version(prebeta: vu.PreBetaVersion, *, dry_run: bool) -> int:
	app_version = vu.prebeta_to_app_version(prebeta)
	print(f"Target pre-beta: {prebeta}")
	print(f"Target AppVersion: {app_version}")
	if dry_run:
		return app_version
	with Step("Update version files"):
		vu.apply_prebeta_version(prebeta)
		print(f"  {vu.VERSION_FILE.relative_to(ROOT)}")
		print(f"  {vu.VERSION_H.relative_to(ROOT)}")
		print(f"  {vu.CLIENT_INFO.relative_to(ROOT)}")
	return app_version


def build_telegram(*, dry_run: bool) -> None:
	if dry_run:
		print("Would build Release target Telegram")
		return
	with Step("Build Whitegram (Release)"):
		if not OUT.exists():
			raise RuntimeError(
				f"Missing {OUT}. Configure CMake first from Telegram directory."
			)
		run(
			[
				"cmake",
				"--build",
				str(OUT),
				"--config",
				"Release",
				"--target",
				"Telegram",
			],
			cwd=TELEGRAM,
		)


def build_packer(*, dry_run: bool) -> None:
	vcxproj = OUT / "Telegram" / "Packer.vcxproj"
	if dry_run:
		print(f"Would build {vcxproj.name}")
		return
	if not vcxproj.exists():
		print(f"Skip Packer build: {vcxproj} not found")
		return
	msbuild = shutil.which("msbuild")
	if not msbuild:
		print("Skip Packer build: msbuild not found in PATH")
		print("Build Packer manually in Visual Studio.")
		return
	with Step("Build Packer"):
		run(
			[
				msbuild,
				str(vcxproj),
				"/p:Configuration=Release",
				"/p:Platform=x64",
				"/m",
			],
		)


def pack_update(*, dry_run: bool) -> Path:
	if not dry_run:
		verify_release_artifacts()
	pack_name = f"tx64upd{vu.read_app_version()}"
	pack_path = UPDATES_DIR / pack_name
	if dry_run:
		print(f"Would create {pack_path}")
		return pack_path
	with Step("Create signed update pack"):
		if not PACK_BAT.exists():
			raise RuntimeError(f"Missing {PACK_BAT}")
		run(["cmd", "/c", str(PACK_BAT)], cwd=ROOT)
		if not pack_path.exists():
			raise RuntimeError(f"Expected pack at {pack_path}")
		vu.update_manifest(vu.read_app_version(), pack_name)
		size_mb = pack_path.stat().st_size / (1024 * 1024)
		print(f"  {pack_path.relative_to(ROOT)} ({size_mb:.1f} MB)")
	return pack_path


def push_update(*, dry_run: bool) -> None:
	if dry_run:
		print("Would upload pack and manifests to VDS")
		return
	with Step("Upload update to VDS"):
		run([sys.executable, str(Path(__file__).with_name("push_update.py"))], cwd=ROOT)


def cmd_status(_: argparse.Namespace) -> int:
	print_status()
	return 0


def cmd_set(args: argparse.Namespace) -> int:
	if not args.version:
		print("Usage: release.bat version 1.0.11")
		return 1
	prebeta = vu.PreBetaVersion.parse(args.version)
	apply_version(prebeta, dry_run=args.dry_run)
	if not args.dry_run:
		print("\nNext: build Release in Visual Studio, then run:")
		print("  release.bat ship")
		print_status()
	return 0


def cmd_bump(args: argparse.Namespace) -> int:
	if not args.bump:
		print("Usage: release.bat bump patch|minor|major [--by N]")
		return 1
	current = vu.read_prebeta_version()
	target = vu.bump(current, args.bump, args.by)
	print(f"Bump {args.bump}: {current} -> {target}")
	apply_version(target, dry_run=args.dry_run)
	if not args.dry_run:
		print("\nNext: build Release in Visual Studio, then run:")
		print("  release.bat ship")
		print_status()
	return 0


def cmd_build(args: argparse.Namespace) -> int:
	build_telegram(dry_run=args.dry_run)
	if not args.no_packer:
		build_packer(dry_run=args.dry_run)
	return 0


def cmd_pack(args: argparse.Namespace) -> int:
	pack_update(dry_run=args.dry_run)
	return 0


def cmd_push(args: argparse.Namespace) -> int:
	push_update(dry_run=args.dry_run)
	return 0


def cmd_ship(args: argparse.Namespace) -> int:
	pack_update(dry_run=args.dry_run)
	if not args.no_push:
		push_update(dry_run=args.dry_run)
	if not args.dry_run:
		print("\nShip finished.")
		print_status()
	return 0


def cmd_release(args: argparse.Namespace) -> int:
	current = vu.read_prebeta_version()
	if args.version:
		target = vu.PreBetaVersion.parse(args.version)
	elif args.no_bump:
		target = current
	else:
		target = vu.bump(current, args.bump, args.by)
	if target != current:
		print(f"Version: {current} -> {target}")
	else:
		print(f"Version: {target}")
	apply_version(target, dry_run=args.dry_run)
	if args.build:
		build_telegram(dry_run=args.dry_run)
		if not args.no_packer:
			build_packer(dry_run=args.dry_run)
	if not args.no_pack:
		pack_update(dry_run=args.dry_run)
	if not args.no_push:
		push_update(dry_run=args.dry_run)
	if not args.dry_run:
		print("\nRelease finished.")
		print_status()
	return 0


def build_parser() -> argparse.ArgumentParser:
	common = argparse.ArgumentParser(add_help=False)
	common.add_argument(
		"--dry-run",
		action="store_true",
		help="Show actions without changing files or running builds",
	)

	epilog = textwrap.dedent("""
		Recommended workflow (Visual Studio build):

		  cd /d E:\\WhitegramProject
		  release.bat version 1.0.11
		  ... build Release | x64 in Visual Studio ...
		  release.bat ship
	""").strip()

	parser = argparse.ArgumentParser(
		prog="release.bat",
		description="Whitegram release: version files, pack, deploy to VDS",
		epilog=epilog,
		formatter_class=argparse.RawDescriptionHelpFormatter,
	)
	sub = parser.add_subparsers(dest="command", required=True)

	status = sub.add_parser(
		"status",
		help="Show current version and artifacts",
		parents=[common],
	)
	status.set_defaults(func=cmd_status)

	version = sub.add_parser(
		"version",
		aliases=("set", "prepare"),
		help="Set pre-beta version in project files (before VS build)",
		parents=[common],
	)
	version.add_argument("version", help="Target pre-beta version major.minor.patch")
	version.set_defaults(func=cmd_set)

	bump = sub.add_parser(
		"bump",
		help="Bump pre-beta version in project files",
		parents=[common],
	)
	bump.add_argument(
		"bump",
		choices=("major", "minor", "patch"),
		help="Which part to increase",
	)
	bump.add_argument(
		"--by",
		type=int,
		default=1,
		help="How much to increase (default: 1)",
	)
	bump.set_defaults(func=cmd_bump)

	ship = sub.add_parser(
		"ship",
		aliases=("publish", "deploy"),
		help="Pack Release build and upload to VDS (after Visual Studio build)",
		parents=[common],
	)
	ship.add_argument(
		"--no-push",
		action="store_true",
		help="Create pack locally without upload",
	)
	ship.set_defaults(func=cmd_ship)

	build = sub.add_parser(
		"build",
		help="Optional: build via cmake/msbuild instead of Visual Studio",
		parents=[common],
	)
	build.add_argument(
		"--no-packer",
		action="store_true",
		help="Skip Packer build",
	)
	build.set_defaults(func=cmd_build)

	pack = sub.add_parser(
		"pack",
		help="Only create tx64upd* pack from Release build",
		parents=[common],
	)
	pack.set_defaults(func=cmd_pack)

	push = sub.add_parser(
		"push",
		help="Only upload existing pack and manifests to VDS",
		parents=[common],
	)
	push.set_defaults(func=cmd_push)

	release = sub.add_parser(
		"release",
		help="Version + ship in one go (build in VS between steps manually)",
		parents=[common],
	)
	release.add_argument(
		"version",
		nargs="?",
		help="Exact pre-beta version. If omitted, bumps by --bump",
	)
	release.add_argument(
		"--bump",
		choices=("major", "minor", "patch"),
		default="patch",
		help="Which part to increase when version is omitted (default: patch)",
	)
	release.add_argument(
		"--by",
		type=int,
		default=1,
		help="How much to increase (default: 1)",
	)
	release.add_argument(
		"--no-bump",
		action="store_true",
		help="Keep current version",
	)
	release.add_argument(
		"--build",
		action="store_true",
		help="Also compile via cmake/msbuild (optional)",
	)
	release.add_argument(
		"--no-packer",
		action="store_true",
		help="Skip Packer build when --build is used",
	)
	release.add_argument(
		"--no-pack",
		action="store_true",
		help="Skip update pack creation",
	)
	release.add_argument(
		"--no-push",
		action="store_true",
		help="Skip upload to VDS",
	)
	release.set_defaults(func=cmd_release)
	return parser


def main() -> int:
	parser = build_parser()
	args = parser.parse_args()
	try:
		return args.func(args)
	except subprocess.CalledProcessError:
		return 1
	except (RuntimeError, ValueError) as error:
		print(f"Error: {error}")
		return 1


if __name__ == "__main__":
	sys.exit(main())
