.PHONY: build test clean cross support-test

build:
	@zig build -Doptimize=ReleaseFast

test:
	@zig build test

clean:
	@rm -rf zig-out .zig-cache

cross:
	@zig build cross -Doptimize=ReleaseFast

support-test:
	@zig build support-test -Doptimize=ReleaseFast
