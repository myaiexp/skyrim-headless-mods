# Cross-compile environment: Linux -> x86_64 Windows (MSVC ABI), headless.
# Source this before configuring/building any SKSE C++ plugin here.
#
#   source tools/skse/cross-env.sh
#
# Provides on PATH: clang-cl, lld-link, llvm-rc, llvm-lib, llvm-dlltool.
# clang/clang-cl come from the system `clang` package; lld-link + llvm-* come
# from the `lld`/`llvm` packages extracted (no root) into ~/.local/llvm-extra
# (they link against the already-installed llvm-libs, so versions must match).

# llvm-extra holds lld-link + llvm-* tools; its lib dir has the liblld*.so they need.
LLVM_EXTRA="$HOME/.local/llvm-extra"
export PATH="$LLVM_EXTRA/usr/bin:$PATH"
export LD_LIBRARY_PATH="$LLVM_EXTRA/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}:/usr/lib"

# Windows SDK + MSVC CRT headers/libs, splatted by xwin (x86_64, desktop).
export XWIN_SDK="$HOME/.local/xwin-sdk"

if [ ! -d "$XWIN_SDK/crt/include" ]; then
	echo "cross-env: XWIN_SDK not found at $XWIN_SDK — run xwin splat (see docs/skse-toolchain.md)" >&2
else
	# Ensure PascalCase .lib symlinks exist for lld-link (no-op after first run).
	bash "$(dirname "${BASH_SOURCE[0]}")/setup-sdk-symlinks.sh" || true
fi
