#!/usr/bin/env bash
# Package the built artifacts into a FOMOD-installable archive for Nexus.
#
#   ./package.sh            build the FOMOD zip from build/ + plugin/build/ into dist/
#
# Run ./build.sh first (this only packages what's already built — it does not compile).
# A FOMOD here adds no install choices (everything is required and installs together);
# it exists for the branded install page (header image + description). A plain zip of
# core/ would install identically.
#
# Archive layout (root of the zip):
#   fomod/info.xml            mod metadata (name, author, version, website)
#   fomod/ModuleConfig.xml    single required component, shows the header + description
#   fomod/images/header.jpg   installer image
#   core/<Data tree>          the files, mapped to Data/ on install
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# --- release identity (Version tracks plugin/src/main.cpp kVersion) ---
NAME="DBVO Dialogue Tweaks"
VERSION="1.0.0"
AUTHOR="Mase"
WEBSITE="https://github.com/myaiexp/skyrim-headless-mods"
CATEGORY="Patches"

BUILD="$HERE/build"
DLL="$HERE/plugin/build/DBVODialogueTweaks.dll"
HEADER="$HERE/media/header.jpg"

DIST="$HERE/dist"
STAGE="$DIST/$NAME"
CORE="$STAGE/core"
FOMOD="$STAGE/fomod"
ZIP="$DIST/${NAME} ${VERSION}.zip"

# --- map each built artifact to its Data-relative destination under core/ ---
declare -A FILES=(
	["$BUILD/Interface/dialoguemenu.swf"]="Interface/dialoguemenu.swf"
	["$BUILD/Scripts/DBVODialogueTweaksMCM.pex"]="Scripts/DBVODialogueTweaksMCM.pex"
	["$BUILD/Scripts/DBVOTweaks.pex"]="Scripts/DBVOTweaks.pex"
	["$BUILD/DBVODialogueTweaks.esp"]="DBVODialogueTweaks.esp"
	["$DLL"]="SKSE/Plugins/DBVODialogueTweaks.dll"
)

# --- preflight: every artifact must exist (else build.sh hasn't run) ---
missing=0
for src in "${!FILES[@]}"; do
	[[ -f "$src" ]] || { echo "ERROR: missing artifact: $src" >&2; missing=1; }
done
[[ -f "$HEADER" ]] || { echo "ERROR: missing installer image: $HEADER" >&2; missing=1; }
if (( missing )); then
	echo "  Run ./build.sh first." >&2
	exit 1
fi

# --- clean stage ---
rm -rf "$STAGE" "$ZIP"
mkdir -p "$CORE" "$FOMOD/images"

# --- stage the files into core/ ---
for src in "${!FILES[@]}"; do
	dst="$CORE/${FILES[$src]}"
	mkdir -p "$(dirname "$dst")"
	cp "$src" "$dst"
done
cp "$HEADER" "$FOMOD/images/header.jpg"

# --- fomod/info.xml ---
cat > "$FOMOD/info.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<fomod>
  <Name>${NAME}</Name>
  <Author>${AUTHOR}</Author>
  <Version>${VERSION}</Version>
  <Website>${WEBSITE}</Website>
  <Description>Pacing and control tweaks for Dragonborn Voice Over (DBVO): the NPC reply lands when your voiced line actually ends, line skip, clean audio cuts, and a player-voice volume slider — all from a SkyUI MCM.</Description>
  <Groups>
    <element>${CATEGORY}</element>
  </Groups>
</fomod>
EOF

# --- fomod/ModuleConfig.xml (single required component) ---
cat > "$FOMOD/ModuleConfig.xml" <<'EOF'
<?xml version="1.0" encoding="utf-8"?>
<config xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:noNamespaceSchemaLocation="http://qconsulting.ca/fo3/ModConfig5.0.xsd">
  <moduleName>DBVO Dialogue Tweaks</moduleName>
  <moduleImage path="fomod\images\header.jpg"/>
  <installSteps order="Explicit">
    <installStep name="DBVO Dialogue Tweaks">
      <optionalFileGroups order="Explicit">
        <group name="Components" type="SelectAll">
          <plugins order="Explicit">
            <plugin name="DBVO Dialogue Tweaks">
              <description>Reply-on-line-end timing, manual line skip, clean cut on skip and interrupt, player-voice volume, and a SkyUI MCM.

Requires Dragonborn Voice Over, SKSE, SkyUI, and Address Library. Let this overwrite DBVO's dialoguemenu.swf.</description>
              <image path="fomod\images\header.jpg"/>
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
