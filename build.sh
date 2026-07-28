#!/usr/bin/env bash
# ============================================================================
# Team 106 build & deploy script
#
# Uses contest board config from: contest2026_106_VelaGoGoGo/board/contest_board/
# Linked to: vendor/openvela/boards/contest2026_106_board/
#
# Usage:
#   ./build.sh                incremental build (daily dev, ~10s)
#   ./build.sh full            full build (first time / after defconfig change)
#   ./build.sh flash           incremental build + flash to device
#   ./build.sh full-flash      full build + flash
# ============================================================================

set -eo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEAM_REPO="$SCRIPT_DIR"
WORKSPACE="$(cd "$TEAM_REPO/.." && pwd)"

# Contest board config (linked via manifest)
BOARD_CONFIG="vendor/openvela/boards/contest2026_106_board/configs/nsh"
NUTTX_DIR="$WORKSPACE/nuttx"

# Vendor paths for rcS.nsh + sys_partition.fex (needs temporary override)
VENDOR_RCS="$WORKSPACE/vendor/allwinnertech/boards/r528/r528s3-gemini-s1/src/etc/init.d/rcS.nsh"
TEAM_RCS="$TEAM_REPO/configs/rcS.nsh"
VENDOR_PART="$WORKSPACE/vendor/allwinnertech/lichee/board/r528s3/gemini-s1_nand/configs/sys_partition.fex"
TEAM_PART="$TEAM_REPO/configs/sys_partition.fex"
VENDOR_GIT="$WORKSPACE/vendor/allwinnertech"
VENDOR_BOARD_MAKEFILE="$WORKSPACE/vendor/allwinnertech/boards/r528/r528s3-gemini-s1/src/Makefile"
CA_ROMFS_RAW="etc/ssl/certs/doubao-ca.pem"
TEAM_CA="$TEAM_REPO/app/home_scense/doubao/certs/doubao-ca.pem"
VENDOR_CA="$WORKSPACE/vendor/allwinnertech/boards/r528/r528s3-gemini-s1/src/$CA_ROMFS_RAW"

export PATH="$WORKSPACE/prebuilts/build-tools/linux-x86_64/bin:$PATH"

die() { echo "ERROR: $*" >&2; exit 1; }

refresh_doubao_objects() {
    # __has_include("doubao_secret.h") is not reliably represented in old
    # generated dependency files. Recompile the coordinator after local
    # credential changes so a package cannot silently retain placeholders.
    find "$TEAM_REPO/app/home_scense/doubao" -maxdepth 1 -type f \
        -name 'doubao_voice.c.*.o' -delete
}



restore_vendor() {
    cd "$VENDOR_GIT"
    git checkout -- \
        "boards/r528/r528s3-gemini-s1/src/etc/init.d/rcS.nsh" \
        "boards/r528/r528s3-gemini-s1/src/Makefile" \
        "boards/r528/r528s3-gemini-s1/src/etc/ssl/certs/doubao-ca.pem" \
        "lichee/board/r528s3/gemini-s1_nand/configs/sys_partition.fex" \
        2>/dev/null || true
    echo "  [restored vendor rcS.nsh + sys_partition.fex]"
}

do_full_build() {
    echo "=== Full build ==="

    # Apply team rcS.nsh + sys_partition.fex (temporary — restored after build)
    if [ -f "$TEAM_RCS" ]; then
        cp "$TEAM_RCS" "$VENDOR_RCS"
        echo "  [applied team rcS.nsh]"
    fi
    if [ -f "$TEAM_PART" ]; then
        cp "$TEAM_PART" "$VENDOR_PART"
        echo "  [applied team sys_partition.fex]"
    fi
    [ -f "$TEAM_CA" ] || die "Missing Doubao CA certificate asset"
    mkdir -p "$(dirname "$VENDOR_CA")"
    cp "$TEAM_CA" "$VENDOR_CA"
    grep -qF "RCRAWS += $CA_ROMFS_RAW" "$VENDOR_BOARD_MAKEFILE" || \
        printf '\nRCRAWS += %s\n' "$CA_ROMFS_RAW" >> "$VENDOR_BOARD_MAKEFILE"
    echo "  [included CA bundle in ROMFS]"

    # NuttX caches ROMFS artifacts and skips regeneration when only the
    # board Makefile (RCRAWS) changes, not .config.
    rm -f "$WORKSPACE/nuttx/etctmp/romfs.c" "$WORKSPACE/nuttx/etctmp/romfs.o"
    rm -rf "$WORKSPACE/nuttx/etctmp/etc/ssl"

    # repo sync forces LFS smudge=--skip on every repo, leaving 134-byte
    # pointer files instead of real .a binaries. Pull the real blobs now
    # so the linker doesn't fail with "file format not recognized".
    echo "  [pulling LFS objects for prebuilt libraries]"
    for libs_repo in "$WORKSPACE"/vendor/openvela/boards/vela/libs; do
      if [ -f "$libs_repo/.gitattributes" ] && git -C "$libs_repo" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        git -C "$libs_repo" config --local filter.lfs.smudge 'git-lfs smudge -- %f'
        git -C "$libs_repo" config --local filter.lfs.process 'git-lfs filter-process'
        git -C "$libs_repo" lfs pull "$(git -C "$libs_repo" remote | head -1)"
        echo "  [lfs pull done: $libs_repo]"
      fi
    done

    # Build
    cd "$WORKSPACE"
    ./build.sh "$BOARD_CONFIG" -j8 || die "full build failed"

    # Restore vendor immediately
    restore_vendor
    echo "=== Full build done ==="
}

do_incremental_build() {
    echo "=== Incremental build ==="
    cd "$WORKSPACE"
    # envsetup.sh references NUTTX_DIR_NAME (must be set for -u mode)
    export NUTTX_DIR_NAME="${NUTTX_DIR_NAME:-nuttx}"
    source build/envsetup.sh 2>/dev/null || true
    make -C "$NUTTX_DIR" EXTRAFLAGS="-Wno-cpp -Wno-deprecated-declarations" -j8 || die "incremental build failed"
    echo "=== Incremental build done ==="
}

do_flash() {
    local vela="$NUTTX_DIR/vela.bin"
    [ -f "$vela" ] || die "$vela not found. Build first."

    echo "=== Flashing to device ==="
    adb push "$vela" /data/vela.bin
    adb shell "dd if=/data/vela.bin of=/dev/bootloader"
    adb shell reboot

    echo "=== Waiting for device (~15s) ==="
    sleep 12
    adb devices -l
    echo "=== Done ==="
}

do_pack_img() {
    echo "=== Packing firmware image ==="

    # Apply team partition config (needed for larger bootloader)
    if [ -f "$TEAM_PART" ]; then
        cp "$TEAM_PART" "$VENDOR_PART"
        echo "  [applied team sys_partition.fex]"
    fi

    LICHEE="$WORKSPACE/vendor/allwinnertech/lichee"
    cd "$LICHEE"
    source tools/scripts/envsetup.sh 2>/dev/null
    rm -rf out
    ./tools/scripts/pack_img.sh \
        -c sun8iw20p1 -p rtos -b r528s3-gemini-s1 -o nuttx -d uart0 \
        -s none -m normal -w none -v none -i none \
        -t "$LICHEE" -f r528s3/gemini-s1_nand -g r528s3/gemini-s1_nand \
        || true  # pack_img.sh exits non-zero but image is generated
    echo ""
    ls -lh "$LICHEE/out/r528s3/gemini-s1_nand/"*.img 2>/dev/null || true

    # Restore vendor partition (envsetup changes CWD)
    git -C "$VENDOR_GIT" checkout -- \
        "lichee/board/r528s3/gemini-s1_nand/configs/sys_partition.fex" \
        2>/dev/null
    echo "  [restored vendor sys_partition.fex]"
    echo "=== Pack done ==="
}

# --- main ---
MODE="${1:-incremental}"

case "$MODE" in
    full)        do_full_build ;;
    flash)       do_incremental_build; do_flash ;;
    full-flash|flash-full) do_full_build; do_flash ;;
    pack)        do_pack_img ;;
    pack-flash)  do_pack_img; do_flash ;;
    *)           do_incremental_build ;;
esac
