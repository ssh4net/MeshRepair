#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"

install_root="${1:-"${repo_root}/build/INSTALL"}"
app_name="MeshRepair.app"

app_path="${install_root}/${app_name}"
addon_dir="${repo_root}/meshrepair_blender"

if [[ ! -d "${app_path}" ]]; then
  echo "error: app not found at: ${app_path}" >&2
  echo "hint: run cmake --install first (install prefix should contain ${app_name})" >&2
  exit 1
fi

if [[ ! -d "${addon_dir}" ]]; then
  echo "error: addon folder not found at: ${addon_dir}" >&2
  exit 1
fi

dist_dir="${install_root}/dist"
mkdir -p "${dist_dir}"

echo "Staging app..."
rm -rf "${dist_dir}/${app_name}"
cp -R "${app_path}" "${dist_dir}/${app_name}"

icon_path="${dist_dir}/${app_name}/Contents/Resources/MeshRepair.icns"
if [[ ! -f "${icon_path}" ]]; then
  echo "warning: app icon not found at: ${icon_path}" >&2
  echo "hint: rebuild/install after adding icon generation, then re-run this script" >&2
fi

# Ensure the CLI/engine is bundled inside the staged .app (required for Blender addon default path).
# Prefer the installed app-bundled copy if present; otherwise fall back to install_root/bin/meshrepair.
engine_dst="${dist_dir}/${app_name}/Contents/MacOS/meshrepair"
if [[ ! -x "${engine_dst}" ]]; then
  engine_src_app="${app_path}/Contents/MacOS/meshrepair"
  engine_src_bin="${install_root}/bin/meshrepair"
  if [[ -x "${engine_src_app}" ]]; then
    echo "Bundling engine from app: ${engine_src_app}"
    cp -f "${engine_src_app}" "${engine_dst}"
  elif [[ -x "${engine_src_bin}" ]]; then
    echo "Bundling engine from bin: ${engine_src_bin}"
    cp -f "${engine_src_bin}" "${engine_dst}"
  else
    echo "warning: meshrepair engine binary not found (expected either '${engine_src_app}' or '${engine_src_bin}')" >&2
  fi
fi

echo "Creating Blender addon zip..."
addon_zip="${dist_dir}/meshrepair_blender.zip"
rm -f "${addon_zip}"

(
  cd "${repo_root}"
  /usr/bin/zip -r -9 "${addon_zip}" "meshrepair_blender" \
    -x "meshrepair_blender/**/__pycache__/*" \
    -x "meshrepair_blender/**/*.pyc" \
    -x "meshrepair_blender/**/.DS_Store"
)

echo "Creating DMG..."
dmg_path="${install_root}/MeshRepair.dmg"
rm -f "${dmg_path}"

/usr/bin/hdiutil create \
  -volname "MeshRepair" \
  -srcfolder "${dist_dir}" \
  -ov \
  -format UDZO \
  "${dmg_path}"

echo "Done:"
echo "  ${dmg_path}"

