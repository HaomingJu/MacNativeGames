#!/bin/bash
# 固定版本着色器工具的构建全部位于外置工作目录。
set -euo pipefail
GAME=$(cd "$(dirname "$0")/.." && pwd)
WORK=$1
DOTNET=$2
BUILD=$(mktemp -d "$WORK/shader-build.XXXXXX")
export TMPDIR="$WORK/tmp" DOTNET_CLI_HOME="$WORK/dotnet-home" NUGET_PACKAGES="$WORK/nuget" DOTNET_CLI_TELEMETRY_OPTOUT=1
mkdir -p "$BUILD/ryujinx-src" "$BUILD/xlate" "$BUILD/spvc" "$WORK/build/shader-service"
cp -R "$GAME/third_party/ryujinx/." "$BUILD/ryujinx-src/"
for p in "$GAME"/tools/totk_xlate/patches/*.patch; do patch -s -d "$BUILD/ryujinx-src" -p1 --forward < "$p"; done
cp "$GAME"/tools/totk_xlate/*.cs "$GAME"/tools/totk_xlate/totk_xlate.csproj "$BUILD/xlate/"
git init -q "$BUILD/spirv-cross"
git -C "$BUILD/spirv-cross" fetch --depth 1 https://github.com/KhronosGroup/SPIRV-Cross.git aa217aeb6c9f0ace7a0ab233b28807edf45eb165
git -C "$BUILD/spirv-cross" checkout -q FETCH_HEAD
for f in spirv_cfg spirv_cpp spirv_cross spirv_cross_parsed_ir spirv_cross_util spirv_glsl spirv_hlsl spirv_msl spirv_parser spirv_reflect; do
 xcrun clang++ -O3 -DNDEBUG -std=c++11 -arch arm64 -Wno-deprecated-declarations -mmacosx-version-min=12.0 -c "$BUILD/spirv-cross/$f.cpp" -o "$BUILD/spvc/$f.o"
done
xcrun clang++ -O3 -DNDEBUG -std=c++11 -arch arm64 -Wno-deprecated-declarations -mmacosx-version-min=12.0 -I"$BUILD/spirv-cross" -c "$GAME/tools/totk_xlate/spirv_cross_inproc.cpp" -o "$BUILD/spvc/inproc.o"
xcrun ar rcs "$BUILD/libtotk_spirv_cross.a" "$BUILD"/spvc/*.o
"$DOTNET" publish "$BUILD/xlate/totk_xlate.csproj" -c Release -r osx-arm64 -p:PublishAot=true -p:InvariantGlobalization=true -p:RyuSrc="$BUILD/ryujinx-src" -p:TotkSpirvCrossLib="$BUILD/libtotk_spirv_cross.a" -o "$WORK/build/shader-service"
shasum -a 256 "$WORK/build/shader-service/totk_xlate" > "$WORK/build/shader-service/tool.sha256"
echo "原生着色器翻译器构建完成"
