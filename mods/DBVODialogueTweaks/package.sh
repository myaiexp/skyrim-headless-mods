#!/usr/bin/env bash
# Package the built artifacts into a FOMOD-installable archive for Nexus.
#
#   ./package.sh            build the FOMOD zip from build/ + plugin/build/ into dist/
#
# Run ./build.sh first (this only packages what's already built — it does not compile).
# The FOMOD carries one real choice: which dialoguemenu.swf to install — the stock-DBVO menu, or
# a UI overhaul's own DBVO-patched menu with our deltas ported onto it (variants/README.md).
# Everything else is required and installs together.
#
# Archive layout (root of the zip):
#   fomod/info.xml            mod metadata (name, author, version, website)
#   fomod/ModuleConfig.xml    required component + the select-one menu-style group
#   fomod/images/header.jpg   installer image
#   core/<Data tree>          esp + Scripts + SKSE/Plugins — always installed
#   ui/stock/Interface/…      the stock-DBVO menu (default)
#   ui/<variant>/Interface/…  one per compatibility variant
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=variants/lib.sh
source "$HERE/variants/lib.sh"

# --- release identity ---
# VERSION is the MOD's release number and normally tracks plugin/src/main.cpp kVersion. 1.1.1 is
# the first release where they legitimately differ in the last digit: it adds nothing to the mod's
# behaviour — the DLL, both .pex and the .esp ship byte-identical to 1.1.0 — it only adds menu
# styles for UI overhauls, which is a compatibility fix on the install side. Hence a patch bump and
# an unchanged kVersion; a minor bump is for the next release that actually moves the C++.
NAME="DBVO Dialogue Tweaks"
VERSION="1.1.1"
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
# The swf is NOT here: it is the one file that differs per menu style, so it ships under ui/.
declare -A FILES=(
	["$BUILD/Scripts/DBVODialogueTweaksMCM.pex"]="Scripts/DBVODialogueTweaksMCM.pex"
	["$BUILD/Scripts/DBVOTweaks.pex"]="Scripts/DBVOTweaks.pex"
	["$BUILD/DBVODialogueTweaks.esp"]="DBVODialogueTweaks.esp"
	["$DLL"]="SKSE/Plugins/DBVODialogueTweaks.dll"
)

# --- menu styles: stock first, then every declared variant, in variant.conf `order=` ---
# A declared variant that did not build is FATAL here (build.sh only warns): a release that
# quietly drops a menu style would strand exactly the users the style exists for.
declare -A UI_SWF=( [stock]="$BUILD/Interface/dialoguemenu.swf" )
UI_IDS=(stock)
for id in $(variant_ids); do
	UI_IDS+=("$id")
	UI_SWF["$id"]="$BUILD/variants/$id/Interface/dialoguemenu.swf"
done

# --- preflight: every artifact must exist (else build.sh hasn't run) ---
missing=0
for src in "${!FILES[@]}"; do
	[[ -f "$src" ]] || { echo "ERROR: missing artifact: $src" >&2; missing=1; }
done
for id in "${UI_IDS[@]}"; do
	[[ -f "${UI_SWF[$id]}" ]] || { echo "ERROR: missing menu style '$id': ${UI_SWF[$id]}" >&2; missing=1; }
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
for id in "${UI_IDS[@]}"; do
	mkdir -p "$STAGE/ui/$id/Interface"
	cp "${UI_SWF[$id]}" "$STAGE/ui/$id/Interface/dialoguemenu.swf"
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
  <Description>DBVO 1.x ONLY — do not install on DBVO 2, it softlocks dialogue. Pacing and control tweaks for Dragonborn Voice Over (DBVO) 1.x: the NPC reply lands when your voiced line actually ends, line skip, clean audio cuts, and a player-voice volume slider — all from a SkyUI MCM.</Description>
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
              <description>DBVO 1.x ONLY. Do NOT install this on DBVO 2 (Dragonborn Voice Over 2) — DBVO 2 ships no dialoguemenu.swf and no Papyrus, so the patched menu here never gets the call that arms the reply and clicking a topic softlocks the conversation. DBVO 2 already does nearly all of this natively (reply timed off your line's real audio length, npc_response_delay, dialogue_volume, and manual skip since 2.0.1.6).

Reply-on-line-end timing, manual line skip, clean cut on skip and interrupt, player-voice volume, and a SkyUI MCM.

Requires Dragonborn Voice Over 1.1.1 (the mod page's OLD FILES tab), SKSE, SkyUI, and Address Library. Let this overwrite DBVO's dialoguemenu.swf — the next page picks WHICH menu that is, so choose your UI overhaul there if you use one.</description>
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
EOF

# --- fomod/ModuleConfig.xml, part 2: the menu-style group (generated from variants/) ---
# Written by loop, not by hand: adding a fifth UI overhaul is a variants/ directory and nothing
# else. `<` and `&` in a description would break the XML, so every interpolated string is escaped.
xml_escape() { sed -e 's/&/\&amp;/g' -e 's/</\&lt;/g' -e 's/>/\&gt;/g'; }

cat >> "$FOMOD/ModuleConfig.xml" <<'EOF'
        <group name="Dialogue menu style" type="SelectExactlyOne">
          <plugins order="Explicit">
            <plugin name="Stock DBVO menu">
              <description>The dialogue menu as Dragonborn Voice Over itself ships it — bottom-centre topic list, vanilla styling. Pick this unless you run one of the UI overhauls below.</description>
              <files>
                <folder source="ui\stock" destination="" priority="0"/>
              </files>
              <typeDescriptor>
                <type name="Recommended"/>
              </typeDescriptor>
            </plugin>
EOF

for id in "${UI_IDS[@]}"; do
	[[ "$id" == stock ]] && continue
	{
		printf '            <plugin name="%s">\n' "$(variant_get "$id" name | xml_escape)"
		printf '              <description>%s\n\nPick this instead of the stock menu when you run %s: it is that mod'"'"'s own DBVO-patched dialogue menu with this mod'"'"'s changes ported onto it, so your dialogue keeps its layout.</description>\n' \
			"$(variant_get "$id" desc | xml_escape)" \
			"$(variant_get "$id" ui_mod | xml_escape)"
		printf '              <files>\n                <folder source="ui\\%s" destination="" priority="0"/>\n              </files>\n' "$id"
		printf '              <typeDescriptor>\n                <type name="Optional"/>\n              </typeDescriptor>\n'
		printf '            </plugin>\n'
	} >> "$FOMOD/ModuleConfig.xml"
done

cat >> "$FOMOD/ModuleConfig.xml" <<'EOF'
          </plugins>
        </group>
      </optionalFileGroups>
    </installStep>
  </installSteps>
</config>
EOF

# --- zip (archive root = fomod/ + core/ + ui/) ---
( cd "$STAGE" && zip -rq "$ZIP" fomod core ui )

echo ">> packaged: $ZIP"
( cd "$STAGE" && find fomod core ui -type f | sort | sed 's/^/   /' )
echo ">> size: $(du -h "$ZIP" | cut -f1)"
