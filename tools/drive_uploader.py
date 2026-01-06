#!/usr/bin/env python3
import argparse
import json
import os
import sys
import time
from pathlib import Path

from google.oauth2.service_account import Credentials
from googleapiclient.discovery import build
from googleapiclient.http import MediaFileUpload


def load_config(config_path: Path):
	with config_path.open("r") as f:
		cfg = json.load(f)
	if "service_account" not in cfg or "folder_id" not in cfg:
		raise ValueError("Config must include 'service_account' and 'folder_id'.")
	cfg.setdefault("mime_overrides", {".json": "application/json", ".jpg": "image/jpeg"})
	return cfg


def drive_client(service_account_json: Path):
	scopes = ["https://www.googleapis.com/auth/drive.file"]
	creds = Credentials.from_service_account_file(str(service_account_json), scopes=scopes)
	return build("drive", "v3", credentials=creds, cache_discovery=False)


def list_candidates(out_dir: Path, exts):
	files = []
	for p in out_dir.glob("**/*"):
		if p.is_file() and p.suffix.lower() in exts:
			files.append(p)
	return files


def load_state(state_path: Path):
	if state_path.exists():
		try:
			return json.loads(state_path.read_text())
		except Exception:
			pass
	return {}


def save_state(state_path: Path, state: dict):
	tmp = state_path.with_suffix(state_path.suffix + ".tmp")
	tmp.write_text(json.dumps(state, indent=2))
	tmp.replace(state_path)


def upload_file(drive, folder_id: str, path: Path, mime: str):
	file_metadata = {"name": path.name, "parents": [folder_id]}
	media = MediaFileUpload(str(path), mimetype=mime, resumable=True)
	file = drive.files().create(body=file_metadata, media_body=media, fields="id").execute()
	return file["id"]


def main():
	ap = argparse.ArgumentParser(description="Upload frames and JSON to Google Drive")
	ap.add_argument("--out-dir", required=True, help="Directory to watch for .jpg/.json")
	ap.add_argument("--config", required=True, help="Path to drive_config.json")
	ap.add_argument("--poll-interval", type=float, default=2.0, help="Polling interval seconds")
	ap.add_argument("--extensions", default=".jpg,.json,.csv", help="Comma-separated list of extensions to upload")
	args = ap.parse_args()

	out_dir = Path(args.out_dir).expanduser().resolve()
	cfg = load_config(Path(args.config).expanduser().resolve())
	exts = {e.strip().lower() for e in args.extensions.split(",") if e.strip()}
	mime_overrides = cfg.get("mime_overrides", {})
	state_path = out_dir / ".upload_state.json"
	state = load_state(state_path)

	if not out_dir.exists():
		print(f"ERROR: out-dir '{out_dir}' does not exist", file=sys.stderr)
		sys.exit(1)

	drive = drive_client(Path(cfg["service_account"]))
	folder_id = cfg["folder_id"]

	print(f"Watching '{out_dir}' for {exts} and uploading to Drive folder {folder_id}...")
	try:
		while True:
			files = list_candidates(out_dir, exts)
			files.sort(key=lambda p: p.stat().st_mtime)
			for path in files:
				sp = str(path)
				if sp in state:
					continue
				# Small delay to avoid incomplete writes
				age = time.time() - path.stat().st_mtime
				if age < 0.5:
					continue
				mime = mime_overrides.get(path.suffix.lower(), "application/octet-stream")
				try:
					file_id = upload_file(drive, folder_id, path, mime)
					state[sp] = {"id": file_id, "ts": int(time.time())}
					save_state(state_path, state)
					print(f"Uploaded: {path} -> {file_id}")
				except Exception as e:
					print(f"Failed upload {path}: {e}", file=sys.stderr)
			time.sleep(args.poll_interval)
	except KeyboardInterrupt:
		print("Exiting.")


if __name__ == "__main__":
	main()

