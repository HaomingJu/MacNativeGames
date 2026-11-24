# 产物仅写到玩家指定的外部工作目录。
WORK ?= $(error 请设置 WORK 为外部工作目录)
OUT := $(WORK)/build
PY ?= $(WORK)/venv/bin/python
REWRITTEN := $(WORK)/rewritten-aligned
TABLES := $(WORK)/tables-aligned/runtime-tables.bin
.PHONY: native-probe probe boot
native-probe: $(OUT)/native-probe
$(OUT)/native-probe: runtime/native_probe.c runtime/gate.S runtime/ipc.c runtime/thread_enter.S runtime/threads.c runtime/host_wrap.c runtime/hle_stack.S
	@mkdir -p "$(OUT)" "$(WORK)/tmp"
	TMPDIR="$(WORK)/tmp" xcrun clang -arch arm64 -std=gnu11 -O2 -g -Wall -Wextra -Werror -Wno-deprecated-declarations -Ithird_party/d2r_gfx/gfx/nvn/include $^ -o "$@"
probe: native-probe
	"$(OUT)/native-probe" "$(TABLES)" "$(REWRITTEN)"
boot: metal-runtime
	PYTHONDONTWRITEBYTECODE=1 "$(PY)" -I scripts/verify_romfs.py "$(WORK)/romfs/romfs.bin"
	"$(OUT)/pmttyd-native" "$(TABLES)" "$(REWRITTEN)" boot "$(WORK)/romfs/romfs.bin"

GFX_SRC := $(wildcard third_party/d2r_gfx/gfx/nvn/*.c third_party/d2r_gfx/gfx/nvn/*.m third_party/d2r_gfx/gfx/nvn/*.S third_party/d2r_gfx/gfx/tex/*.c third_party/d2r_gfx/gfx/tex/*.m)
GFX_HEADERS := $(wildcard third_party/d2r_gfx/gfx/nvn/*.h third_party/d2r_gfx/gfx/nvn/*.inc third_party/d2r_gfx/gfx/nvn/include/*.h third_party/d2r_gfx/gfx/tex/*.h)
GFX_OBJECTS := $(patsubst %,$(OUT)/obj/%.o,$(GFX_SRC))
.PHONY: graphics
# NVN API 与 Metal 后端先独立编译，宿主集成随后链接。
graphics: $(GFX_OBJECTS)
$(OUT)/obj/%.o: % $(GFX_HEADERS)
	@mkdir -p "$(dir $@)"
	TMPDIR="$(WORK)/tmp" xcrun clang -arch arm64 -std=gnu11 -O2 -g -fobjc-arc -Wno-unused-command-line-argument -Ithird_party/d2r_gfx/gfx/nvn/include -c "$<" -o "$@"

.PHONY: metal-runtime
metal-runtime: $(OUT)/pmttyd-native
$(OUT)/pmttyd-native: runtime/native_probe.c runtime/gate.S runtime/ipc.c runtime/thread_enter.S runtime/host_wrap.c runtime/hle_stack.S runtime/gfx_bridge.c runtime/input_bridge.c runtime/hid_controller.m runtime/frontend.m runtime/audio_renderer.c runtime/audio_mixer.c runtime/audio_reverb.c runtime/audio_effects.c runtime/opus_codec.c runtime/save_host.c runtime/threads.c ../../shared/runtime/audio_out.c ../../shared/runtime/movie_host.c runtime/movie_host.h runtime/game_info.plist Makefile $(GFX_OBJECTS)
	TMPDIR="$(WORK)/tmp" xcrun clang -arch arm64 -std=gnu11 -O2 -g -DPMTTYD_METAL=1 -Iruntime -Wl,-sectcreate,__TEXT,__info_plist,runtime/game_info.plist -fobjc-arc -Wno-deprecated-declarations -Ithird_party/d2r_gfx/gfx/nvn/include -I$(OPUS_PREFIX)/include/opus -I$(VPX_PREFIX)/include $(filter %.c %.m %.S %.o,$^) $(OPUS_PREFIX)/lib/libopus.a $(VPX_PREFIX)/lib/libvpx.a -framework Cocoa -framework Metal -framework MetalFX -framework QuartzCore -framework ImageIO -framework CoreGraphics -framework CoreText -framework UniformTypeIdentifiers -framework CoreVideo -framework CoreFoundation -framework AudioToolbox -framework GameController -framework CoreHaptics -framework IOKit -framework VideoToolbox -framework CoreMedia -o "$@"

VPX_PREFIX ?= $(shell python3 ../../tools/deps prefix libvpx)
OPUS_PREFIX ?= $(shell python3 ../../tools/deps prefix opus)

.PHONY: prepare verify-rewrite
prepare:
	PYTHONDONTWRITEBYTECODE=1 "$(PY)" scripts/rewrite.py "$(WORK)/exefs" --out "$(REWRITTEN)"
	PYTHONDONTWRITEBYTECODE=1 "$(PY)" scripts/prepare_runtime.py "$(REWRITTEN)" "$(WORK)/exefs" --out "$(WORK)/tables-aligned"
verify-rewrite:
	@for m in main sdk subsdk0 rtld; do TMPDIR="$(WORK)/tmp" PYTHONDONTWRITEBYTECODE=1 "$(PY)" third_party/d2r/structck.py "$(REWRITTEN)" $$m "$(WORK)/exefs/$$m" || exit $$?; done

SELFTEST_SRC := $(filter-out runtime/frontend.m,$(wildcard runtime/*.c runtime/*.m runtime/*.S)) tests/drive_frontend.m ../../shared/runtime/audio_out.c ../../shared/runtime/movie_host.c
.PHONY: selftest-runtime
selftest-runtime: $(OUT)/pmttyd-selftest
$(OUT)/pmttyd-selftest: $(SELFTEST_SRC) runtime/movie_host.h $(GFX_OBJECTS) Makefile
	TMPDIR="$(WORK)/tmp" xcrun clang -arch arm64 -std=gnu11 -O2 -g -DPMTTYD_METAL=1 -Iruntime -DPMTTYD_SELFTEST=1 -fobjc-arc -Wno-deprecated-declarations -Ithird_party/d2r_gfx/gfx/nvn/include -I$(OPUS_PREFIX)/include/opus -I$(VPX_PREFIX)/include $(filter %.c %.m %.S %.o,$^) $(OPUS_PREFIX)/lib/libopus.a $(VPX_PREFIX)/lib/libvpx.a -framework Cocoa -framework Metal -framework MetalFX -framework QuartzCore -framework ImageIO -framework CoreGraphics -framework CoreText -framework UniformTypeIdentifiers -framework CoreVideo -framework CoreFoundation -framework AudioToolbox -framework GameController -framework CoreHaptics -framework IOKit -framework VideoToolbox -framework CoreMedia -o "$@"

.PHONY: test-audio-out
test-audio-out: $(OUT)/audio-out-test
	"$(OUT)/audio-out-test"
$(OUT)/audio-out-test: tests/audio_out_test.c ../../shared/runtime/audio_out.c runtime/runtime.h runtime/ipc.h ../../shared/runtime/ipc.h
	@mkdir -p "$(OUT)" "$(WORK)/tmp"
	TMPDIR="$(WORK)/tmp" xcrun clang -arch arm64 -std=gnu11 -O2 -Wall -Wextra -Werror -Iruntime "$<" -framework AudioToolbox -framework CoreFoundation -o "$@"

.PHONY: movie-test
movie-test: $(OUT)/movie-test
$(OUT)/movie-test: tests/movie_test.c ../../shared/runtime/movie_host.c runtime/movie_host.h runtime/runtime.h ../../shared/runtime/movie_pace.h
	@mkdir -p "$(OUT)" "$(WORK)/tmp"
	TMPDIR="$(WORK)/tmp" xcrun clang -arch arm64 -std=gnu11 -O2 -Wall -Wextra -Werror -Iruntime -I$(VPX_PREFIX)/include $(filter %.c,$^) $(VPX_PREFIX)/lib/libvpx.a -o "$@"

.PHONY: test-time
test-time: $(OUT)/time-test
	"$(OUT)/time-test"
$(OUT)/time-test: tests/time_test.c runtime/native_probe.c runtime/gate.S runtime/ipc.c runtime/thread_enter.S runtime/threads.c runtime/host_wrap.c runtime/hle_stack.S
	@mkdir -p "$(OUT)" "$(WORK)/tmp"
	TMPDIR="$(WORK)/tmp" xcrun clang -arch arm64 -std=gnu11 -O2 -g -Wall -Wextra -Werror -Wno-deprecated-declarations -Ithird_party/d2r_gfx/gfx/nvn/include $(filter-out runtime/native_probe.c,$^) -o "$@"

.PHONY: test-integer-clear
test-integer-clear: $(OUT)/integer-clear-test
	"$(OUT)/integer-clear-test"
$(OUT)/integer-clear-test: tests/integer_clear_test.c runtime/native_probe.c $(filter-out runtime/native_probe.c tests/drive_frontend.m,$(SELFTEST_SRC)) runtime/frontend.m $(GFX_OBJECTS) Makefile
	TMPDIR="$(WORK)/tmp" xcrun clang -arch arm64 -std=gnu11 -O2 -g -DPMTTYD_METAL=1 -Iruntime -fobjc-arc -Wno-deprecated-declarations -Ithird_party/d2r_gfx/gfx/nvn/include -I$(OPUS_PREFIX)/include/opus -I$(VPX_PREFIX)/include $(filter-out runtime/native_probe.c,$(filter %.c %.m %.S %.o,$^)) $(OPUS_PREFIX)/lib/libopus.a $(VPX_PREFIX)/lib/libvpx.a -framework Cocoa -framework Metal -framework MetalFX -framework QuartzCore -framework ImageIO -framework CoreGraphics -framework CoreText -framework UniformTypeIdentifiers -framework CoreVideo -framework CoreFoundation -framework AudioToolbox -framework GameController -framework CoreHaptics -framework IOKit -framework VideoToolbox -framework CoreMedia -o "$@"

.PHONY: test-preemption
test-preemption: $(OUT)/preemption-test
	"$(OUT)/preemption-test"
$(OUT)/preemption-test: tests/preemption_test.c runtime/native_probe.c runtime/gate.S runtime/ipc.c runtime/thread_enter.S runtime/threads.c runtime/host_wrap.c runtime/hle_stack.S
	@mkdir -p "$(OUT)" "$(WORK)/tmp"
	TMPDIR="$(WORK)/tmp" xcrun clang -arch arm64 -std=gnu11 -O2 -g -Wall -Wextra -Werror -Wno-deprecated-declarations -Ithird_party/d2r_gfx/gfx/nvn/include $(filter-out runtime/native_probe.c,$^) -o "$@"
