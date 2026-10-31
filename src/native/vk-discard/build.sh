#!/bin/sh
# Build VK_LAYER_X4MP_discard and its synthetic test (needs gcc and libvulkan-dev).
# Usage: src/native/vk-discard/build.sh OUTPUT_DIR
set -eu
here=$(dirname "$0")
out=${1:?output directory}
mkdir -p "$out"
gcc -std=c11 -O2 -Wall -Wextra -Werror -fPIC -shared -fvisibility=hidden -Wl,-Bsymbolic \
    -o "$out/libVkLayer_x4mp_discard.so" "$here/vk_discard.c" -lpthread
cp "$here/VkLayer_x4mp_discard.json" "$out/"
gcc -std=c11 -O1 -o "$out/test_discard" "$here/test_discard.c" -lvulkan
echo "built $out/libVkLayer_x4mp_discard.so"
echo "test: X4MP_VK_DISCARD=1 VK_LAYER_PATH=$out VK_INSTANCE_LAYERS=VK_LAYER_X4MP_discard $out/test_discard"
