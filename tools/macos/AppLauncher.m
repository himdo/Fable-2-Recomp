// Native Finder entry point. Game content and writable state stay outside the app.
#import <AppKit/AppKit.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

static void Fail(NSString *message) {
  [NSApplication sharedApplication];
  [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
  [NSApp activateIgnoringOtherApps:YES];
  NSAlert *alert = [NSAlert new];
  alert.messageText = @"Fable II could not start";
  alert.informativeText = message;
  [alert runModal];
  exit(1);
}
static BOOL ValidContent(NSString *path) {
  if (!path.length) return NO;
  NSFileManager *fm = NSFileManager.defaultManager;
  BOOL directory = NO;
  // Reject directories named default.xex before handing the path to the engine.
  return path.isAbsolutePath &&
      [fm fileExistsAtPath:[path stringByAppendingPathComponent:@"default.xex"] isDirectory:&directory] && !directory &&
      [fm fileExistsAtPath:[path stringByAppendingPathComponent:@"data"] isDirectory:&directory] && directory;
}
static void CopyDefault(NSString *source, NSString *destination) {
  NSFileManager *fm = NSFileManager.defaultManager;
  if ([fm fileExistsAtPath:destination]) return;
  NSError *error = nil;
  if (![fm copyItemAtPath:source toPath:destination error:&error])
    Fail([NSString stringWithFormat:@"Could not create %@: %@", destination, error.localizedDescription]);
}
int main(int argc, char **argv) {
  @autoreleasepool {
    NSFileManager *fm = NSFileManager.defaultManager;
    NSBundle *bundle = NSBundle.mainBundle;
    NSString *resources = bundle.resourcePath;
    NSString *engine = [bundle.executablePath.stringByDeletingLastPathComponent stringByAppendingPathComponent:@"fable_2"];
    NSDictionary *environment = NSProcessInfo.processInfo.environment;
    NSString *state = environment[@"FABLE2_APP_STATE_ROOT"];
    if (!state.length) {
      NSURL *support = [fm URLsForDirectory:NSApplicationSupportDirectory inDomains:NSUserDomainMask].firstObject;
      state = [support.path stringByAppendingPathComponent:@"Fable II Recomp"];
    }
    if (!state.isAbsolutePath) Fail(@"The application data folder must be an absolute path.");
    NSError *error = nil;
    for (NSString *part in @[@"", @"logs", @"saves", @"cache", @"metadata"]) {
      if (![fm createDirectoryAtPath:[state stringByAppendingPathComponent:part]
          withIntermediateDirectories:YES attributes:nil error:&error]) Fail(error.localizedDescription);
    }
    // Hold this lock through exec. It also serializes first-run setup.
    int lock = open([[state stringByAppendingPathComponent:@"app.lock"] fileSystemRepresentation], O_CREAT | O_RDWR, 0600);
    if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB) != 0) Fail(@"Another copy of Fable II is already using this application data folder.");
    NSTask *check = [NSTask new];
    check.executableURL = [NSURL fileURLWithPath:@"/usr/bin/pgrep"];
    check.arguments = @[@"-x", @"fable_2"];
    check.standardOutput = [NSFileHandle fileHandleWithNullDevice];
    if (![check launchAndReturnError:&error]) Fail(error.localizedDescription);
    [check waitUntilExit];
    if (check.terminationStatus == 0) Fail(@"Fable II is already running. Close the game before opening another build.");
    if (check.terminationStatus != 1) Fail(@"Could not check whether Fable II is already running.");

    NSString *settingsPath = [state stringByAppendingPathComponent:@"launcher.plist"];
    NSDictionary *settings = [NSDictionary dictionaryWithContentsOfFile:settingsPath];
    NSDictionary *local = [NSDictionary dictionaryWithContentsOfFile:[resources stringByAppendingPathComponent:@"LocalDefaults.plist"]];
    NSString *content = environment[@"FABLE2_GAME_DATA_ROOT"] ?: settings[@"GameDataRoot"] ?: local[@"GameDataRoot"];
    if (!ValidContent(content)) {
      [NSApplication sharedApplication];
      [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
      [NSApp activateIgnoringOtherApps:YES];
      NSOpenPanel *panel = [NSOpenPanel openPanel];
      panel.title = @"Choose your Fable II game folder";
      panel.message = @"Choose the folder containing default.xex and the data folder from your copy of the game.";
      panel.canChooseFiles = NO;
      panel.canChooseDirectories = YES;
      panel.allowsMultipleSelection = NO;
      if ([panel runModal] != NSModalResponseOK) return 0;
      content = panel.URL.path;
      if (!ValidContent(content)) Fail(@"That folder does not contain default.xex and a data folder. Open the app again and choose your extracted game folder.");
    }
    if (![@{@"GameDataRoot": content} writeToFile:settingsPath atomically:YES]) Fail(@"Could not save the game folder selection.");
    for (NSString *name in @[@"fable2_config.toml", @"fable2_patches.toml"])
      CopyDefault([resources stringByAppendingPathComponent:name], [state stringByAppendingPathComponent:name]);
    setenv("FABLE2_APP_STATE_ROOT", state.fileSystemRepresentation, 1);
    if (!getenv("FABLE_STARTUP_LOCK_HANDOFF")) setenv("FABLE_STARTUP_LOCK_HANDOFF", "1", 1);
    if (chdir(state.fileSystemRepresentation) != 0) Fail(@"Could not open the application data folder.");
    NSString *log = [state stringByAppendingPathComponent:@"logs/launcher.log"];
    int output = open(log.fileSystemRepresentation, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (output < 0) Fail(@"Could not open the launch log.");
    dup2(output, STDOUT_FILENO);
    dup2(output, STDERR_FILENO);
    close(output);
    BOOL metal = ![[bundle objectForInfoDictionaryKey:@"Fable2Backend"] isEqualToString:@"vulkan"];
    if (!metal) {
      if (!getenv("REX_VULKAN_LOG_DEBUG_MESSAGES")) setenv("REX_VULKAN_LOG_DEBUG_MESSAGES", "0", 1);
      if (!getenv("MVK_CONFIG_LOG_LEVEL")) setenv("MVK_CONFIG_LOG_LEVEL", "1", 1);
    }
    NSMutableArray<NSString *> *args = [NSMutableArray arrayWithArray:@[
      engine, @"--game_data_root", content, @"--fullscreen=false",
      [@"--log_file=" stringByAppendingString:[state stringByAppendingPathComponent:@"logs/fable_2.log"]]]];
    // Keep the tuned Metal profile aligned with run_macos_metal.command.
    // Diagnostic logging remains opt-in through the environment.
    if (metal) [args addObjectsFromArray:@[
      @"--gpu_plugin=metal", @"--async_shader_compilation=true", @"--metal_pipeline_creation_threads=2",
      @"--metal_type0_register_batching=true", @"--metal_constant_payload_cache=false",
      @"--metal_force_bc_decompress=false", @"--metal_backend_telemetry=false",
      @"--metal_root_rebuild_detail_telemetry=false", @"--metal_probe_capture_interval=0"]];
    else [args addObject:@"--gpu_plugin=xenos"];
    // Finder may provide a legacy process-serial-number argument.
    for (int i = 1; i < argc; ++i) {
      NSString *arg = [NSString stringWithUTF8String:argv[i]];
      if (![arg hasPrefix:@"-psn_"]) [args addObject:arg];
    }
    char **child = calloc(args.count + 1, sizeof(char *));
    for (NSUInteger i = 0; i < args.count; ++i) child[i] = strdup(args[i].UTF8String);
    execv(engine.fileSystemRepresentation, child);
    Fail([NSString stringWithFormat:@"Could not launch the game: %s", strerror(errno)]);
  }
}
