#!/usr/bin/env python3
"""
TestFlight build + upload automation for TGSpeechBox.

Usage:
    python3 testflight.py                  # build + upload both platforms
    python3 testflight.py --macos-only     # macOS only
    python3 testflight.py --ios-only       # iOS only
    python3 testflight.py --skip-libs      # skip CMake rebuild
    python3 testflight.py --no-upload      # build archives only, don't upload
    python3 testflight.py crashes          # pull crash reports from API

Requires:
    - API key at ~/.appstoreconnect/private_keys/AuthKey_<KEY_ID>.p8
    - pip3 install PyJWT cryptography
"""

import argparse
import json
import os
import plistlib
import re
import subprocess
import sys
import time

# ── Config ──────────────────────────────────────────────────────────────
KEY_ID = "Y5KKT3T33Q"
ISSUER_ID = "69a6de72-61a0-47e3-e053-5b8c7c11a4d1"
KEY_FILE = os.path.expanduser(f"~/.appstoreconnect/private_keys/AuthKey_{KEY_ID}.p8")
APP_ID = "6759512621"
TEAM_ID = "P84QQE42U9"

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_YML = os.path.join(SCRIPT_DIR, "project.yml")
XCODEPROJ = os.path.join(SCRIPT_DIR, "TGSpeechBox.xcodeproj")
ARCHIVE_DIR = "/tmp/tgsb-archives"
EXPORT_DIR = "/tmp/tgsb-exports"


# ── JWT ─────────────────────────────────────────────────────────────────
def make_token():
    import jwt
    with open(KEY_FILE) as f:
        key = f.read()
    now = int(time.time())
    payload = {
        "iss": ISSUER_ID,
        "iat": now,
        "exp": now + 1200,
        "aud": "appstoreconnect-v1",
    }
    return jwt.encode(payload, key, algorithm="ES256", headers={"kid": KEY_ID})


def api_get(path, token=None):
    if token is None:
        token = make_token()
    import urllib.request, urllib.error
    url = f"https://api.appstoreconnect.apple.com{path}"
    req = urllib.request.Request(url, headers={"Authorization": f"Bearer {token}"})
    try:
        with urllib.request.urlopen(req) as resp:
            return json.loads(resp.read())
    except urllib.error.HTTPError as e:
        if e.code == 404:
            return {"data": []}
        raise


# ── Version bump ────────────────────────────────────────────────────────
def get_current_version():
    with open(PROJECT_YML) as f:
        text = f.read()
    m = re.search(r'CURRENT_PROJECT_VERSION:\s*"(\d+)"', text)
    return int(m.group(1)) if m else 0


def bump_version():
    old = get_current_version()
    new = old + 1
    with open(PROJECT_YML) as f:
        text = f.read()
    text = text.replace(
        f'CURRENT_PROJECT_VERSION: "{old}"',
        f'CURRENT_PROJECT_VERSION: "{new}"',
    )
    with open(PROJECT_YML, "w") as f:
        f.write(text)
    print(f"  Version bumped: {old} -> {new}")
    return new


# ── Build helpers ───────────────────────────────────────────────────────
def run(cmd, desc=None, cwd=None):
    if desc:
        print(f"  {desc}...")
    result = subprocess.run(
        cmd, shell=True, cwd=cwd or SCRIPT_DIR,
        capture_output=True, text=True
    )
    if result.returncode != 0:
        print(f"  FAILED: {cmd}")
        # Show last 30 lines of stderr/stdout
        output = (result.stdout + "\n" + result.stderr).strip()
        for line in output.split("\n")[-30:]:
            print(f"    {line}")
        sys.exit(1)
    return result.stdout


def build_static_libs():
    print("\n=== Building static libraries ===")
    ncpu = os.cpu_count() or 4

    # macOS desktop
    run(f"cmake -B build/desktop -S . -DCMAKE_BUILD_TYPE=MinSizeRel",
        "Configuring macOS static libs")
    run(f"cmake --build build/desktop -j{ncpu}",
        "Building macOS static libs")

    # iOS
    run("cmake -B build/ios -S . -DCMAKE_BUILD_TYPE=MinSizeRel "
        "-DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=arm64 "
        "-DCMAKE_OSX_SYSROOT=iphoneos",
        "Configuring iOS static libs")
    run(f"cmake --build build/ios -j{ncpu}",
        "Building iOS static libs")


def xcodegen():
    print("\n=== Generating Xcode project ===")
    run("xcodegen generate", "Running xcodegen")


def write_export_plist(method="app-store-connect"):
    path = os.path.join(ARCHIVE_DIR, "ExportOptions.plist")
    plist = {
        "method": method,
        "teamID": TEAM_ID,
        "signingStyle": "automatic",
        "uploadBitcode": False,
        "uploadSymbols": True,
    }
    with open(path, "wb") as f:
        plistlib.dump(plist, f)
    return path


def archive_and_export(scheme, platform_flag, archive_name):
    archive_path = os.path.join(ARCHIVE_DIR, f"{archive_name}.xcarchive")
    export_path = os.path.join(EXPORT_DIR, archive_name)

    print(f"\n=== Archiving {archive_name} ===")
    cmd = (
        f"xcodebuild archive"
        f" -project {XCODEPROJ}"
        f" -scheme {scheme}"
        f" {platform_flag}"
        f" -archivePath {archive_path}"
        f" -allowProvisioningUpdates"
        f" ARCHS=arm64"
    )
    run(cmd, f"Archiving {scheme}")

    print(f"\n=== Exporting {archive_name} ===")
    export_plist = write_export_plist()
    cmd = (
        f"xcodebuild -exportArchive"
        f" -archivePath {archive_path}"
        f" -exportPath {export_path}"
        f" -exportOptionsPlist {export_plist}"
        f" -allowProvisioningUpdates"
    )
    run(cmd, f"Exporting {scheme}")
    return export_path


def find_ipa_or_pkg(export_path):
    for ext in (".ipa", ".pkg"):
        for f in os.listdir(export_path):
            if f.endswith(ext):
                return os.path.join(export_path, f)
    return None


# ── Upload ──────────────────────────────────────────────────────────────
def upload(export_path, platform_name):
    print(f"\n=== Uploading {platform_name} ===")
    artifact = find_ipa_or_pkg(export_path)
    if not artifact:
        print(f"  ERROR: No .ipa or .pkg found in {export_path}")
        sys.exit(1)

    print(f"  Uploading {os.path.basename(artifact)}...")
    cmd = (
        f"xcrun altool --upload-app"
        f" -f {artifact}"
        f" -t {'ios' if platform_name == 'iOS' else 'macos'}"
        f" --apiKey {KEY_ID}"
        f" --apiIssuer {ISSUER_ID}"
    )
    run(cmd, f"Uploading to App Store Connect")
    print(f"  {platform_name} uploaded successfully!")


# ── Poll processing ─────────────────────────────────────────────────────
def wait_for_processing(version_str):
    print(f"\n=== Waiting for build {version_str} to process ===")
    token = make_token()
    for attempt in range(60):  # up to 30 minutes
        data = api_get(
            f"/v1/builds?filter%5Bapp%5D={APP_ID}"
            f"&filter%5Bversion%5D={version_str}"
            f"&fields%5Bbuilds%5D=version,processingState",
            token=token,
        )
        states = []
        for b in data.get("data", []):
            state = b["attributes"]["processingState"]
            states.append(state)
        if states:
            print(f"  Attempt {attempt+1}: {', '.join(states)}")
            if all(s == "VALID" for s in states):
                print("  All builds processed!")
                return True
            if any(s == "FAILED" for s in states):
                print("  ERROR: Build processing failed!")
                return False
        else:
            print(f"  Attempt {attempt+1}: Not yet visible...")

        # Refresh token every 15 minutes
        if attempt > 0 and attempt % 30 == 0:
            token = make_token()
        time.sleep(30)

    print("  Timed out waiting for processing.")
    return False


# ── Crash reports ───────────────────────────────────────────────────────
def pull_crashes():
    print("\n=== Crash Reports ===")
    token = make_token()

    # Get all builds
    data = api_get(
        f"/v1/builds?filter%5Bapp%5D={APP_ID}"
        f"&sort=-uploadedDate&limit=10"
        f"&fields%5Bbuilds%5D=version,uploadedDate,processingState",
        token=token,
    )

    for b in data.get("data", []):
        bid = b["id"]
        attrs = b["attributes"]
        ver = attrs["version"]
        uploaded = attrs["uploadedDate"]
        print(f"\n--- Build v{ver} (uploaded {uploaded}) ---")

        # Diagnostic signatures
        diag = api_get(
            f"/v1/builds/{bid}/diagnosticSignatures?limit=50",
            token=token,
        )
        sigs = diag.get("data", [])
        if not sigs:
            print("  No crash signatures (Apple may still be processing)")
            continue

        for sig in sigs:
            sa = sig["attributes"]
            sig_id = sig["id"]
            dtype = sa.get("diagnosticType", "?")
            weight = sa.get("weight", "?")
            signature = sa.get("signature", "")
            print(f"  [{dtype}] weight={weight}")
            print(f"    {signature[:200]}")

            # Try to get logs for this signature
            logs = api_get(
                f"/v1/diagnosticSignatures/{sig_id}/logs?limit=5",
                token=token,
            )
            for log in logs.get("data", []):
                log_url = log.get("attributes", {}).get("diagnosticLogUrl")
                if log_url:
                    print(f"    Log: {log_url}")


# ── Main ────────────────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(description="TGSpeechBox TestFlight automation")
    parser.add_argument("command", nargs="?", default="build",
                        choices=["build", "crashes"],
                        help="Command to run (default: build)")
    parser.add_argument("--macos-only", action="store_true")
    parser.add_argument("--ios-only", action="store_true")
    parser.add_argument("--skip-libs", action="store_true",
                        help="Skip CMake static lib rebuild")
    parser.add_argument("--no-upload", action="store_true",
                        help="Archive and export only, don't upload")
    parser.add_argument("--no-bump", action="store_true",
                        help="Don't auto-bump build version")
    args = parser.parse_args()

    if args.command == "crashes":
        pull_crashes()
        return

    # Validate
    if not os.path.exists(KEY_FILE):
        print(f"ERROR: API key not found at {KEY_FILE}")
        sys.exit(1)

    do_macos = not args.ios_only
    do_ios = not args.macos_only

    os.makedirs(ARCHIVE_DIR, exist_ok=True)
    os.makedirs(EXPORT_DIR, exist_ok=True)

    # 1. Bump version
    if not args.no_bump:
        print("\n=== Bumping build version ===")
        new_ver = bump_version()
    else:
        new_ver = get_current_version()
        print(f"\n  Using existing version: {new_ver}")

    # 2. Static libs
    if not args.skip_libs:
        build_static_libs()
    else:
        print("\n  Skipping static lib rebuild (--skip-libs)")

    # 3. Xcodegen
    xcodegen()

    # 4. Archive + export
    if do_macos:
        macos_export = archive_and_export(
            "TGSpeechBox-macOS",
            "-destination 'generic/platform=macOS'",
            "TGSpeechBox-macOS",
        )

    if do_ios:
        ios_export = archive_and_export(
            "TGSpeechBox-iOS",
            "-destination 'generic/platform=iOS'",
            "TGSpeechBox-iOS",
        )

    if args.no_upload:
        print("\n=== Done (--no-upload) ===")
        return

    # 5. Upload
    if do_macos:
        upload(macos_export, "macOS")
    if do_ios:
        upload(ios_export, "iOS")

    # 6. Wait for processing
    wait_for_processing(str(new_ver))

    print("\n=== All done! ===")
    print(f"  Build {new_ver} uploaded to TestFlight.")
    print("  Check App Store Connect for beta review status.")


if __name__ == "__main__":
    main()
