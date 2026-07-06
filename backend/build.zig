const std = @import("std");

fn collectCppSources(b: *std.Build, dir_path: []const u8) []const []const u8 {
    var sources: std.ArrayList([]const u8) = .empty;
    const root = b.build_root.handle;
    var dir = root.openDir(b.graph.io, dir_path, .{ .iterate = true }) catch return sources.toOwnedSlice(b.allocator) catch &.{};
    defer dir.close(b.graph.io);
    var walker = dir.walk(b.allocator) catch return sources.toOwnedSlice(b.allocator) catch &.{};
    defer walker.deinit();
    while (walker.next(b.graph.io) catch null) |entry| {
        if (entry.kind == .file and std.mem.endsWith(u8, entry.basename, ".cpp")) {
            const p = std.fmt.allocPrint(b.allocator, "{s}/{s}", .{ dir_path, entry.path }) catch continue;
            sources.append(b.allocator, p) catch continue;
        }
    }
    return sources.toOwnedSlice(b.allocator) catch &.{};
}

fn collectCppSourcesExcludingMultiple(b: *std.Build, dir_path: []const u8, excludes: []const []const u8) []const []const u8 {
    var sources: std.ArrayList([]const u8) = .empty;
    const root = b.build_root.handle;
    var dir = root.openDir(b.graph.io, dir_path, .{ .iterate = true }) catch return sources.toOwnedSlice(b.allocator) catch &.{};
    defer dir.close(b.graph.io);
    var walker = dir.walk(b.allocator) catch return sources.toOwnedSlice(b.allocator) catch &.{};
    defer walker.deinit();
    while (walker.next(b.graph.io) catch null) |entry| {
        if (entry.kind == .file and std.mem.endsWith(u8, entry.basename, ".cpp")) {
            const p = std.fmt.allocPrint(b.allocator, "{s}/{s}", .{ dir_path, entry.path }) catch continue;
            var excluded = false;
            for (excludes) |ex| {
                if (std.mem.eql(u8, p, ex)) {
                    excluded = true;
                    break;
                }
            }
            if (!excluded) sources.append(b.allocator, p) catch continue;
        }
    }
    return sources.toOwnedSlice(b.allocator) catch &.{};
}

fn collectCppSourcesExcluding(b: *std.Build, dir_path: []const u8, exclude: []const u8) []const []const u8 {
    var sources: std.ArrayList([]const u8) = .empty;
    const root = b.build_root.handle;
    var dir = root.openDir(b.graph.io, dir_path, .{ .iterate = true }) catch return sources.toOwnedSlice(b.allocator) catch &.{};
    defer dir.close(b.graph.io);
    var walker = dir.walk(b.allocator) catch return sources.toOwnedSlice(b.allocator) catch &.{};
    defer walker.deinit();
    while (walker.next(b.graph.io) catch null) |entry| {
        if (entry.kind == .file and std.mem.endsWith(u8, entry.basename, ".cpp")) {
            const p = std.fmt.allocPrint(b.allocator, "{s}/{s}", .{ dir_path, entry.path }) catch continue;
            if (std.mem.eql(u8, p, exclude)) continue;
            sources.append(b.allocator, p) catch continue;
        }
    }
    return sources.toOwnedSlice(b.allocator) catch &.{};
}

const cpp_flags: []const []const u8 = &.{
    "-std=c++17",
    "-fno-exceptions",
    "-fno-rtti",
    "-Wall",
    "-Wextra",
};

// stl_parser.cpp includes microstl which uses throw — compile with exceptions enabled at this boundary
const cpp_flags_with_exceptions: []const []const u8 = &.{
    "-std=c++17",
    "-fno-rtti",
    "-Wall",
    "-Wextra",
};

fn configureExe(
    b: *std.Build,
    target: std.Build.ResolvedTarget,
    optimize: std.builtin.OptimizeMode,
) *std.Build.Step.Compile {
    const exe = b.addExecutable(.{
        .name = "guava-slicer-backend",
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
            .link_libcpp = true,
        }),
    });

    const src_files = collectCppSourcesExcluding(b, "src", "src/stl_parser.cpp");
    exe.root_module.addCSourceFiles(.{
        .files = src_files,
        .flags = cpp_flags,
    });

    exe.root_module.addCSourceFiles(.{
        .files = &.{"src/stl_parser.cpp"},
        .flags = cpp_flags_with_exceptions,
    });

    // Clipper2 uses exceptions internally
    exe.root_module.addCSourceFiles(.{
        .files = &.{
            "vendor/clipper2/src/clipper.engine.cpp",
            "vendor/clipper2/src/clipper.offset.cpp",
            "vendor/clipper2/src/clipper.rectclip.cpp",
        },
        .flags = cpp_flags_with_exceptions,
    });

    exe.root_module.addIncludePath(b.path("src"));
    exe.root_module.addSystemIncludePath(b.path("vendor"));
    exe.root_module.addSystemIncludePath(b.path("vendor/clipper2/include"));

    return exe;
}

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const exe = configureExe(b, target, optimize);
    b.installArtifact(exe);

    // Run step
    const run_step = b.step("run", "Run guava-slicer-backend");
    const run_cmd = b.addRunArtifact(exe);
    run_cmd.step.dependOn(b.getInstallStep());
    if (b.args) |args| {
        run_cmd.addArgs(args);
    }
    run_step.dependOn(&run_cmd.step);

    // Test step
    const test_step = b.step("test", "Run tests");
    const test_exe = b.addExecutable(.{
        .name = "guava-slicer-backend-tests",
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
            .link_libcpp = true,
        }),
    });

    const test_files = collectCppSources(b, "tests");
    test_exe.root_module.addCSourceFiles(.{
        .files = test_files,
        .flags = &.{ "-std=c++17", "-Wall", "-Wextra" },
    });

    // Exclude main.cpp and stl_parser.cpp from standard flags
    const test_src_files = collectCppSourcesExcludingMultiple(b, "src", &.{ "src/main.cpp", "src/stl_parser.cpp" });
    test_exe.root_module.addCSourceFiles(.{
        .files = test_src_files,
        .flags = cpp_flags,
    });
    test_exe.root_module.addCSourceFiles(.{
        .files = &.{"src/stl_parser.cpp"},
        .flags = cpp_flags_with_exceptions,
    });
    test_exe.root_module.addCSourceFiles(.{
        .files = &.{
            "vendor/clipper2/src/clipper.engine.cpp",
            "vendor/clipper2/src/clipper.offset.cpp",
            "vendor/clipper2/src/clipper.rectclip.cpp",
        },
        .flags = cpp_flags_with_exceptions,
    });

    test_exe.root_module.addIncludePath(b.path("src"));
    test_exe.root_module.addSystemIncludePath(b.path("vendor"));
    test_exe.root_module.addSystemIncludePath(b.path("vendor/clipper2/include"));
    test_exe.root_module.addIncludePath(b.path("../../../libs/doctest"));

    const run_tests = b.addRunArtifact(test_exe);
    test_step.dependOn(&run_tests.step);

    // Cross-compilation step
    const cross_step = b.step("cross", "Cross-compile for all supported targets");
    const CrossTarget = struct {
        arch: std.Target.Cpu.Arch,
        os: std.Target.Os.Tag,
        abi: ?std.Target.Abi = null,
        dir: []const u8,
    };
    const cross_targets: []const CrossTarget = &.{
        .{ .arch = .aarch64, .os = .macos, .dir = "mac-arm" },
        .{ .arch = .x86_64, .os = .macos, .dir = "mac-intel" },
        .{ .arch = .x86_64, .os = .windows, .abi = .gnu, .dir = "win-x86_64" },
    };

    for (cross_targets) |ct| {
        var query: std.Target.Query = .{};
        query.cpu_arch = ct.arch;
        query.os_tag = ct.os;
        if (ct.abi) |a| query.abi = a;
        const resolved = b.resolveTargetQuery(query);
        const ct_exe = configureExe(b, resolved, optimize);
        const install = b.addInstallArtifact(ct_exe, .{
            .dest_dir = .{ .override = .{ .custom = ct.dir } },
        });
        cross_step.dependOn(&install.step);
    }
}
