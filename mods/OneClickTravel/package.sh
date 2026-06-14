#!/usr/bin/env bash
# Package the built DLL into a FOMOD-installable archive for Nexus.
#
#   ./package.sh            build the FOMOD zip from build/ into dist/
#
# Run ./build.sh first (this only packages what's already built — it does not compile).
#
# A FOMOD here adds NO install choices — OneClickTravel is a single required DLL with nothing
# to pick. It exists purely for the branded install page (header image + description), to match
# the DBVO Dialogue Tweaks release shape (mods/DBVODialogueTweaks/package.sh). A plain zip of
# core/ would install identically; the FOMOD is presentation, not function.
#
# Archive layout (root of the zip):
#   fomod/info.xml             mod metadata (name, author, version, website)
#   fomod/ModuleConfig.xml     single required component, shows the header + description
#   fomod/images/header.<ext>  installer image (from media/header.{png,jpg})
#   core/SKSE/Plugins/OneClickTravel.dll   the one file, mapped to Data/ on install
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# --- release identity (Version tracks src/main.cpp kVersion) ---
DISPLAY_NAME="One-Click Map — Instant Fast Travel"  # install-page + Nexus title
NAME="OneClickTravel"                               # DLL/plugin name + archive basename (filesystem-clean)
VERSION="1.0.0"
AUTHOR="Mase"
WEBSITE="https://github.com/myaiexp/skyrim-headless-mods"
CATEGORY="User Interface"

DLL="$HERE/build/OneClickTravel.dll"

# Installer image — drop a header into media/ as png or jpg (you generate this; it's also the
# Nexus page header). First match wins.
HEADER=""
for cand in "$HERE/media/header.png" "$HERE/media/header.jpg" "$HERE/media/header.jpeg"; do
	[[ -f "$cand" ]] && { HEADER="$cand"; break; }
done

DIST="$HERE/dist"
STAGE="$DIST/$NAME"
CORE="$STAGE/core"
FOMOD="$STAGE/fomod"
ZIP="$DIST/${NAME} ${VERSION}.zip"

# --- preflight: the DLL and the header image must both exist ---
missing=0
if [[ ! -f "$DLL" ]]; then
	echo "ERROR: missing artifact: $DLL" >&2
	echo "  Run ./build.sh first." >&2
	missing=1
fi
if [[ -z "$HEADER" ]]; then
	echo "ERROR: missing installer image — drop a header at $HERE/media/header.png (or .jpg)" >&2
	echo "  It's the FOMOD install image and the Nexus page header." >&2
	missing=1
fi
(( missing )) && exit 1

HEADER_EXT="${HEADER##*.}"
HEADER_REL="fomod/images/header.${HEADER_EXT}"
HEADER_WIN="fomod\\images\\header.${HEADER_EXT}"  # FOMOD paths use backslashes

# --- clean stage, lay the DLL at its Data-relative path + the image under fomod/ ---
rm -rf "$STAGE" "$ZIP"
mkdir -p "$CORE/SKSE/Plugins" "$FOMOD/images"
cp "$DLL" "$CORE/SKSE/Plugins/OneClickTravel.dll"
cp "$HEADER" "$STAGE/$HEADER_REL"

# --- fomod/info.xml ---
cat > "$FOMOD/info.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<fomod>
  <Name>${DISPLAY_NAME}</Name>
  <Author>${AUTHOR}</Author>
  <Version>${VERSION}</Version>
  <Website>${WEBSITE}</Website>
  <Description>Click a discovered location on the world map and you travel instantly — the vanilla "Fast travel to X?" confirmation box is suppressed before it renders. One SKSE DLL, no .esp, no scripts; stacks cleanly with map-replacer mods.</Description>
  <Groups>
    <element>${CATEGORY}</element>
  </Groups>
</fomod>
EOF

# --- fomod/ModuleConfig.xml (single required component — no choices, just the branded page) ---
cat > "$FOMOD/ModuleConfig.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<config xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:noNamespaceSchemaLocation="http://qconsulting.ca/fo3/ModConfig5.0.xsd">
  <moduleName>${DISPLAY_NAME}</moduleName>
  <moduleImage path="${HEADER_WIN}"/>
  <installSteps order="Explicit">
    <installStep name="${DISPLAY_NAME}">
      <optionalFileGroups order="Explicit">
        <group name="Component" type="SelectAll">
          <plugins order="Explicit">
            <plugin name="OneClickTravel (SKSE DLL)">
              <description>Instant fast travel from the world map: clicking a discovered location suppresses the vanilla confirmation box and starts the trip directly. Every other map interaction and message box stays vanilla.

Requires SKSE64 and Address Library for SKSE Plugins. Restart Skyrim after install.</description>
              <image path="${HEADER_WIN}"/>
              <files>
                <folder source="core" destination="" priority="0"/>
              </files>
              <typeDescriptor>
                <type name="Required"/>
              </typeDescriptor>
            </plugin>
          </plugins>
        </group>
      </optionalFileGroups>
    </installStep>
  </installSteps>
</config>
EOF

# --- zip (archive root = fomod/ + core/) ---
( cd "$STAGE" && zip -rq "$ZIP" fomod core )

echo ">> packaged: $ZIP"
( cd "$STAGE" && find fomod core -type f | sort | sed 's/^/   /' )
echo ">> size: $(du -h "$ZIP" | cut -f1)"
