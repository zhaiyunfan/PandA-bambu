#!/bin/bash
set -e

workspace_dir=$PWD

function cleanup {
   echo "::endgroup::"
   make --directory=$workspace_dir -f Makefile.init clean
}
trap cleanup EXIT

echo "::group::Initialize workspace"
export PATH=/usr/lib/ccache:$PATH
mkdir -p ~/.ccache/
cat > ~/.ccache/ccache.conf << EOF
max_size = 5.0G
cache_dir = $workspace_dir/.ccache
EOF
if [[ -d "dist" ]]; then
   echo "Pre-initialized dist dir found. Installing system wide..."
   cp -r dist/. /
fi

if [[ -d "compiler" ]]; then
   echo "Bambu compiler dir found. Installing system wide..."
   cp -r compiler/. /
fi

GCC_BINS=("`find /usr/bin -type f -regextype posix-extended -regex '.*g(cc|\+\+)-[0-9]+\.?[0-9]?'`")
CLANG_BINS=("`find /clang+llvm-*/bin -type f -regextype posix-extended -regex '.*clang-[0-9]+\.?[0-9]?'`")
cd /usr/bin
for clang_exe in $CLANG_BINS
do
   CLANG_VER=$(sed 's/clang-//g' <<< "$(basename $clang_exe)")
   CLANG_DIR=$(dirname $clang_exe)
   echo "Generating system links for clang/llvm $CLANG_VER"
   ln -sf "$CLANG_DIR/clang-$CLANG_VER" "clang-$CLANG_VER"
   ln -sf "$CLANG_DIR/clang-$CLANG_VER" "clang++-$CLANG_VER"
   ln -sf "$CLANG_DIR/clang-$CLANG_VER" "clang-cpp-$CLANG_VER"
   ln -sf "$CLANG_DIR/llvm-config" "llvm-config-$CLANG_VER"
   ln -sf "$CLANG_DIR/llvm-link" "llvm-link-$CLANG_VER"
   ln -sf "$CLANG_DIR/opt" "opt-$CLANG_VER"
done
mkdir -p "$workspace_dir/dist/usr/bin"
cd "$workspace_dir/dist/usr/bin"
for clang_exe in $CLANG_BINS
do
   CLANG_VER=$(sed 's/clang-//g' <<< "$(basename $clang_exe)")
   CLANG_DIR=$(dirname $clang_exe)
   echo "Generating dist links for clang/llvm $CLANG_VER"
   ln -sf "../..$CLANG_DIR/clang-$CLANG_VER" "clang-$CLANG_VER"
   ln -sf "../..$CLANG_DIR/clang-$CLANG_VER" "clang++-$CLANG_VER"
   ln -sf "../..$CLANG_DIR/clang-$CLANG_VER" "clang-cpp-$CLANG_VER"
   ln -sf "../..$CLANG_DIR/llvm-config" "llvm-config-$CLANG_VER"
   ln -sf "../..$CLANG_DIR/llvm-link" "llvm-link-$CLANG_VER"
   ln -sf "../..$CLANG_DIR/opt" "opt-$CLANG_VER"
done
cd /usr/lib/ccache
for compiler in $GCC_BINS
do
   echo "Generating ccache alias for $(basename $compiler)"
   ln -sf ../../bin/ccache "$(basename $compiler)"
done
for compiler in $CLANG_BINS
do
   CLANG_VER=$(sed 's/clang-//g' <<< "$(basename $compiler)")
   echo "Generating ccache alias for clang-$CLANG_VER"
   ln -sf ../../bin/ccache "clang-$CLANG_VER"
   echo "Generating ccache alias for clang++-$CLANG_VER"
   ln -sf ../../bin/ccache "clang++-$CLANG_VER"
done
cd $workspace_dir

max_gcc_ver="$(ls -x -v -1a dist/usr/include/c++ 2> /dev/null | tail -1)"
if [[ -z "${max_gcc_ver}" ]]
then
  echo "At least one gcc version must be bundled in the AppImage"
  exit -1
fi
echo "Latest bundled GCC version: ${max_gcc_ver}"

echo "Initializing build environment..."
make -f Makefile.init
echo "::endgroup::"

echo "::group::Configure build environment"
mkdir build
cd build
../configure --prefix=/usr $@
cd ..
echo "::endgroup::"

echo "::group::Build Bambu"
make --directory=build -j$J install-strip DESTDIR="$workspace_dir/dist"
echo "::endgroup"

echo "::group::Package Appimage"

rm -f `find dist -type f -name clang-tidy`
rm -f `find dist -type f -name clang-query`
rm -f `find dist -type f -name clang-change-namespace`
rm -f `find dist -type f -name clang-reorder-fields`
rm -f `find dist -type f -name clang-func-mapping`
rm -f `find dist -type f -name sancov`
rm -f dist/clang+llvm*/lib/*.a
rm -rf dist/usr/share/man

echo "Inflating libraries..."
mkdir dist/lib
mkdir dist/lib/x86_64-linux-gnu/
cp /lib/x86_64-linux-gnu/libtinfo.so.5.* dist/lib/x86_64-linux-gnu/
ln -s /lib/x86_64-linux-gnu/libtinfo.so.5.* dist/lib/x86_64-linux-gnu/libtinfo.so.5
cp /usr/lib/libbdd.so.0.0.0 dist/usr/lib/libbdd.so.0.0.0
cd dist/usr/lib/
ln -s libbdd.so.0.0.0 libbdd.so.0
cd ../../..

echo "Inflating metadata..."
cp style/img/panda.png.in dist/bambu.png
cat > dist/bambu.desktop << EOF
[Desktop Entry]
Name=bambu
Exec=tool_select.sh
Icon=bambu
Type=Application
Terminal=true
Categories=Development;
EOF
cat > dist/usr/bin/tool_select.sh << EOF
#!/bin/bash
export LC_ALL="C"
BINARY_NAME=\$(basename "\$ARGV0")
BINARY_PATH="\$APPDIR/usr/bin/\$BINARY_NAME"
if [ ! -e "\$BINARY_PATH" ]; then
   BINARY_PATH="\$APPDIR/usr/bin/bambu"
fi
\$BINARY_PATH "\$@"
EOF
chmod a+x dist/usr/bin/tool_select.sh

echo "Generating appimage..."
curl -L https://github.com/AppImage/AppImageKit/releases/download/continuous/AppRun-x86_64 -o dist/AppRun -s
chmod +x dist/AppRun
ARCH=x86_64 appimagetool dist 2> /dev/null

echo "::set-output name=appimage::$(ls *.AppImage)"
echo "::set-output name=dist-dir::dist"
