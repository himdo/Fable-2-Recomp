import SwiftUI
import AppKit

// MARK: - Models

struct GameStatus: Codable {
    var installed: Bool = false
    var profileName: String = "No instalado"
    var profileId: String = ""
    var compatible: Bool = false
    var xexHash: String = ""
    var executablePresent: Bool = false
    var recentFps: String = "N/A"
    var saveCount: Int = 0
}

struct ExtractionProgress: Codable {
    var copied: Int64
    var total: Int64
    var percent: Double
    var file: String
}

// MARK: - ViewModel

@MainActor
class LauncherViewModel: ObservableObject {
    @Published var status: GameStatus = GameStatus()
    @Published var selectedPreset: String = "enhanced"
    @Published var isFullscreen: Bool = true
    @Published var selectedResolution: String = "retina"
    
    // Patches
    @Published var skipIntroVideos: Bool = true
    @Published var unlockWebsite: Bool = true
    @Published var unlockCe: Bool = true
    @Published var disableMotionBlur: Bool = false
    @Published var realtimeTextureMorphing: Bool = true
    @Published var mouseLook: Bool = true
    @Published var highTickRate: Bool = false

    // Extraction state
    @Published var isExtracting: Bool = false
    @Published var extractionPercent: Double = 0.0
    @Published var extractionFile: String = ""
    @Published var extractionError: String? = nil
    @Published var selectedIsoPath: String = ""
    @Published var showReextract: Bool = false

    // Logs & Alerts
    @Published var alertMessage: String? = nil
    @Published var showAlert: Bool = false

    @Published var repoRoot: URL
    var launcherScript: URL

    static func detectGameRoot() -> URL {
        if let env = ProcessInfo.processInfo.environment["FABLE2_ROOT"], !env.isEmpty {
            let u = URL(fileURLWithPath: env)
            if FileManager.default.fileExists(atPath: u.path) { return u }
        }
        if let saved = UserDefaults.standard.string(forKey: "Fable2GameRoot"), !saved.isEmpty {
            let u = URL(fileURLWithPath: saved)
            if FileManager.default.fileExists(atPath: u.path) { return u }
        }
        let cwd = URL(fileURLWithPath: FileManager.default.currentDirectoryPath)
        if FileManager.default.fileExists(atPath: cwd.appendingPathComponent("CMakeLists.txt").path) ||
           FileManager.default.fileExists(atPath: cwd.appendingPathComponent("tools/mac/fable2_launcher.py").path) ||
           FileManager.default.fileExists(atPath: cwd.appendingPathComponent("default.xex").path) {
            return cwd
        }
        for startURL in [Bundle.main.bundleURL, Bundle.main.bundleURL.resolvingSymlinksInPath()] {
            var candidate = startURL
            for _ in 0..<8 {
                candidate = candidate.deletingLastPathComponent()
                if FileManager.default.fileExists(atPath: candidate.appendingPathComponent("tools/mac/fable2_launcher.py").path) ||
                   FileManager.default.fileExists(atPath: candidate.appendingPathComponent("CMakeLists.txt").path) ||
                   FileManager.default.fileExists(atPath: candidate.appendingPathComponent("default.xex").path) {
                    return candidate
                }
            }
        }
        let standard = URL(fileURLWithPath: "/Users/antoniorodriguez/GITHUB-RASTY/Fable-2-Recomp")
        if FileManager.default.fileExists(atPath: standard.path) {
            return standard
        }
        return cwd
    }

    static func detectLauncherScript(gameRoot: URL) -> URL {
        if let bundled = Bundle.main.url(forResource: "fable2_launcher", withExtension: "py"),
           FileManager.default.fileExists(atPath: bundled.path) {
            return bundled
        }
        let inRoot = gameRoot.appendingPathComponent("tools/mac/fable2_launcher.py")
        if FileManager.default.fileExists(atPath: inRoot.path) {
            return inRoot
        }
        return URL(fileURLWithPath: "/Users/antoniorodriguez/GITHUB-RASTY/Fable-2-Recomp/tools/mac/fable2_launcher.py")
    }

    init() {
        let root = Self.detectGameRoot()
        self.repoRoot = root
        self.launcherScript = Self.detectLauncherScript(gameRoot: root)
        refreshStatus()
        loadConfig()
    }

    func setGameRoot(_ newUrl: URL) {
        self.repoRoot = newUrl
        self.launcherScript = Self.detectLauncherScript(gameRoot: newUrl)
        UserDefaults.standard.set(newUrl.path, forKey: "Fable2GameRoot")
        self.refreshStatus()
        self.loadConfig()
    }

    func selectGameFolder() {
        let panel = NSOpenPanel()
        panel.canChooseFiles = false
        panel.canChooseDirectories = true
        panel.allowsMultipleSelection = false
        panel.message = "Selecciona la carpeta donde está instalado o se extraerá Fable 2"
        panel.prompt = "Seleccionar Carpeta"
        if panel.runModal() == .OK, let url = panel.url {
            setGameRoot(url)
        }
    }

    private func createPythonProcess(arguments: [String]) -> Process {
        let task = Process()
        task.executableURL = URL(fileURLWithPath: "/usr/bin/python3")
        task.arguments = arguments
        var env = ProcessInfo.processInfo.environment
        env["FABLE2_ROOT"] = repoRoot.path
        env["PYTHONUNBUFFERED"] = "1"
        task.environment = env
        return task
    }

    func refreshStatus() {
        let task = createPythonProcess(arguments: [launcherScript.path, "verify", "--dir", repoRoot.path])
        
        let pipe = Pipe()
        task.standardOutput = pipe
        task.standardError = Pipe()

        do {
            try task.run()
            task.waitUntilExit()
            let data = pipe.fileHandleForReading.readDataToEndOfFile()
            if let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any] {
                self.status.installed = json["installed"] as? Bool ?? false
                self.status.profileName = json["profile_name"] as? String ?? "Desconocido"
                self.status.profileId = json["profile_id"] as? String ?? ""
                self.status.compatible = json["compatible"] as? Bool ?? false
                self.status.xexHash = json["xex_hash"] as? String ?? ""
            }
        } catch {
            print("Error checking status: \(error)")
        }

        // Check executable
        let exePath = repoRoot.appendingPathComponent("out/build/mac-arm64-release/fable_2").path
        self.status.executablePresent = FileManager.default.fileExists(atPath: exePath)

        // Check saves count
        let savesDir = repoRoot.appendingPathComponent("out/build/mac-arm64-release/saves")
        if let subdirs = try? FileManager.default.subpathsOfDirectory(atPath: savesDir.path) {
            self.status.saveCount = subdirs.filter { $0.hasSuffix("mainsave.bin") }.count
        }

        // Check recent FPS from log
        checkRecentFps()
    }

    func checkRecentFps() {
        let logsDir = repoRoot.appendingPathComponent("out/build/mac-arm64-release/logs")
        guard let files = try? FileManager.default.contentsOfDirectory(at: logsDir, includingPropertiesForKeys: [.contentModificationDateKey], options: .skipsHiddenFiles) else {
            return
        }
        let sorted = files.filter { $0.lastPathComponent.hasPrefix("fable_2_") && $0.pathExtension == "log" }
            .sorted { (u1, u2) -> Bool in
                let d1 = (try? u1.resourceValues(forKeys: [.contentModificationDateKey]))?.contentModificationDate ?? Date.distantPast
                let d2 = (try? u2.resourceValues(forKeys: [.contentModificationDateKey]))?.contentModificationDate ?? Date.distantPast
                return d1 > d2
            }
        if let latest = sorted.first, let content = try? String(contentsOf: latest, encoding: .utf8) {
            let lines = content.components(separatedBy: .newlines).filter { $0.contains("guest vblank pacer") }
            if let lastLine = lines.suffix(10).first {
                if let range = lastLine.range(of: "~") {
                    let sub = lastLine[range.upperBound...]
                    if let hzRange = sub.range(of: " Hz") {
                        self.status.recentFps = "\(sub[..<hzRange.lowerBound]) FPS"
                    }
                }
            }
        }
    }

    func loadConfig() {
        let f2Config = repoRoot.appendingPathComponent("out/build/mac-arm64-release/fable2_config.toml")
        if let content = try? String(contentsOf: f2Config, encoding: .utf8) {
            skipIntroVideos = content.contains("skip_intro_videos = true")
            unlockWebsite = content.contains("unlock_website = true")
            unlockCe = content.contains("unlock_ce = true")
            disableMotionBlur = content.contains("disable_motion_blur = true")
            realtimeTextureMorphing = content.contains("realtime_texture_morphing = true")
            highTickRate = content.contains("high_tick_rate = true")
            mouseLook = content.contains("mouse_look = true")
        }
    }

    func applyPreset(_ presetName: String) {
        selectedPreset = presetName
        let task = createPythonProcess(arguments: [launcherScript.path, "preset", presetName])
        try? task.run()
        task.waitUntilExit()

        switch presetName {
        case "original":
            selectedResolution = "720p"
            isFullscreen = false
            highTickRate = false
        case "balanced":
            selectedResolution = "1080p"
            isFullscreen = false
            highTickRate = false
        case "enhanced":
            selectedResolution = "retina"
            isFullscreen = true
            highTickRate = false
        case "high-fps":
            selectedResolution = "1080p"
            isFullscreen = false
            highTickRate = true
        default:
            break
        }
    }

    func saveConfiguration() {
        // Update fable2_config.toml
        setRecompPatch("skip_intro_videos", value: skipIntroVideos ? "true" : "false")
        setRecompPatch("unlock_website", value: unlockWebsite ? "true" : "false")
        setRecompPatch("unlock_ce", value: unlockCe ? "true" : "false")
        setRecompPatch("disable_motion_blur", value: disableMotionBlur ? "true" : "false")
        setRecompPatch("realtime_texture_morphing", value: realtimeTextureMorphing ? "true" : "false")
        setRecompPatch("high_tick_rate", value: highTickRate ? "true" : "false")
        setRecompPatch("higher_hf_tick_rate", value: highTickRate ? "true" : "false")
        setRecompInput("mouse_look", value: mouseLook ? "true" : "false")

        // Update fable_2.toml
        setSdkValue("fullscreen", value: isFullscreen ? "true" : "false")
        let resMap: [String: (String, String)] = [
            "720p": ("1280", "720"),
            "1080p": ("1920", "1080"),
            "1440p": ("2560", "1440"),
            "retina": ("3024", "1964")
        ]
        if let (w, h) = resMap[selectedResolution] {
            setSdkValue("window_width", value: w)
            setSdkValue("window_height", value: h)
        }
    }

    private func setRecompPatch(_ key: String, value: String) {
        let task = createPythonProcess(arguments: [launcherScript.path, "config", "set", "patches.\(key)", value])
        try? task.run()
        task.waitUntilExit()
    }

    private func setRecompInput(_ key: String, value: String) {
        let task = createPythonProcess(arguments: [launcherScript.path, "config", "set", "input.\(key)", value])
        try? task.run()
        task.waitUntilExit()
    }

    private func setSdkValue(_ key: String, value: String) {
        let task = createPythonProcess(arguments: [launcherScript.path, "config", "set", key, value])
        try? task.run()
        task.waitUntilExit()
    }

    func selectIsoFile() {
        let panel = NSOpenPanel()
        panel.canChooseFiles = true
        panel.canChooseDirectories = false
        panel.allowsMultipleSelection = false
        panel.allowedContentTypes = []
        panel.message = "Selecciona la ISO de Fable 2 (Xbox 360)"
        panel.prompt = "Seleccionar"
        
        if panel.runModal() == .OK, let url = panel.url {
            selectedIsoPath = url.path
        }
    }

    func startExtraction() {
        guard !selectedIsoPath.isEmpty else { return }
        let isoUrl = URL(fileURLWithPath: selectedIsoPath)
        guard FileManager.default.fileExists(atPath: isoUrl.path) else {
            self.extractionError = "El archivo ISO no existe en: \(selectedIsoPath)"
            return
        }

        let scriptUrl = launcherScript
        guard FileManager.default.fileExists(atPath: scriptUrl.path) else {
            self.extractionError = "Script auxiliar no encontrado en: \(scriptUrl.path)"
            return
        }

        isExtracting = true
        extractionPercent = 0.0
        extractionFile = "Iniciando extracción..."
        extractionError = nil

        let isoPath = isoUrl.path
        let scriptPath = scriptUrl.path
        let rootPath = repoRoot.path

        Task.detached {
            let task = Process()
            task.executableURL = URL(fileURLWithPath: "/usr/bin/python3")
            task.arguments = [scriptPath, "extract", isoPath, "--dest", rootPath, "--json"]
            var env = ProcessInfo.processInfo.environment
            env["FABLE2_ROOT"] = rootPath
            env["PYTHONUNBUFFERED"] = "1"
            task.environment = env
            
            let outPipe = Pipe()
            let errPipe = Pipe()
            task.standardOutput = outPipe
            task.standardError = errPipe

            do {
                try task.run()

                let outHandle = outPipe.fileHandleForReading
                let errHandle = errPipe.fileHandleForReading
                var buffer = Data()

                while true {
                    let chunk = outHandle.availableData
                    if chunk.isEmpty {
                        break // EOF reached
                    }
                    buffer.append(chunk)

                    while let newlineIndex = buffer.firstIndex(of: 10) { // \n
                        let lineData = buffer.subdata(in: 0..<newlineIndex)
                        buffer.removeSubrange(0...newlineIndex)

                        if let json = try? JSONSerialization.jsonObject(with: lineData) as? [String: Any] {
                            if let pct = json["percent"] as? Double,
                               let file = json["file"] as? String {
                                await MainActor.run {
                                    self.extractionPercent = pct / 100.0
                                    self.extractionFile = file
                                }
                            }
                        }
                    }
                }

                task.waitUntilExit()
                let errData = errHandle.readDataToEndOfFile()
                let errOutput = String(data: errData, encoding: .utf8) ?? ""

                await MainActor.run {
                    self.isExtracting = false
                    if task.terminationStatus == 0 {
                        self.refreshStatus()
                        self.showReextract = false
                        self.alertMessage = "¡Extracción completada con éxito!"
                        self.showAlert = true
                    } else {
                        let trimmedErr = errOutput.trimmingCharacters(in: .whitespacesAndNewlines)
                        if !trimmedErr.isEmpty {
                            self.extractionError = trimmedErr
                        } else {
                            self.extractionError = "Error en la extracción (código \(task.terminationStatus))."
                        }
                    }
                }
            } catch {
                await MainActor.run {
                    self.isExtracting = false
                    self.extractionError = "Error al iniciar proceso: \(error.localizedDescription)"
                }
            }
        }
    }

    func launchGame() {
        saveConfiguration()
        var args = [launcherScript.path, "run", "--detach", "--resolution", selectedResolution]
        if isFullscreen {
            args.append("--fullscreen=true")
        } else {
            args.append("--fullscreen=false")
        }

        let task = createPythonProcess(arguments: args)
        do {
            try task.run()
        } catch {
            alertMessage = "Error al lanzar el juego: \(error.localizedDescription)"
            showAlert = true
        }
    }

    func openSavesFolder() {
        let savesDir = repoRoot.appendingPathComponent("out/build/mac-arm64-release/saves")
        NSWorkspace.shared.open(savesDir)
    }

    func openLogsFolder() {
        let logsDir = repoRoot.appendingPathComponent("out/build/mac-arm64-release/logs")
        NSWorkspace.shared.open(logsDir)
    }
}

// MARK: - SwiftUI Views

struct ContentView: View {
    @StateObject var vm = LauncherViewModel()

    var body: some View {
        VStack(spacing: 0) {
            // Header
            HeaderView(status: vm.status)

            Divider()

            // Main Content Area
            ScrollView {
                VStack(spacing: 18) {
                    // Status & ISO Extraction Card
                    InstallationCard(vm: vm)

                    // Presets
                    PresetsCard(selectedPreset: $vm.selectedPreset, onSelect: vm.applyPreset)

                    // Display Settings
                    DisplaySettingsCard(isFullscreen: $vm.isFullscreen, selectedResolution: $vm.selectedResolution)

                    // Patches & Controls
                    PatchesCard(vm: vm)
                }
                .padding(20)
            }

            Divider()

            // Bottom Action Bar
            BottomBarView(vm: vm)
        }
        .frame(minWidth: 680, minHeight: 640)
        .background(Color(NSColor.windowBackgroundColor))
        .alert(isPresented: $vm.showAlert) {
            Alert(title: Text("Fable II Launcher"), message: Text(vm.alertMessage ?? ""), dismissButton: .default(Text("Aceptar")))
        }
    }
}

// MARK: - Subviews

struct HeaderView: View {
    let status: GameStatus

    var body: some View {
        HStack(spacing: 16) {
            Image(systemName: "shield.checkered")
                .font(.system(size: 36))
                .foregroundColor(.accentColor)

            VStack(alignment: .leading, spacing: 3) {
                Text("Fable II Recompiled")
                    .font(.title2)
                    .fontWeight(.bold)
                Text("Port nativo para macOS (Apple Silicon)")
                    .font(.subheadline)
                    .foregroundColor(.secondary)
            }

            Spacer()

            VStack(alignment: .trailing, spacing: 4) {
                HStack(spacing: 6) {
                    Circle()
                        .fill(status.installed ? Color.green : Color.orange)
                        .frame(width: 9, height: 9)
                    Text(status.installed ? status.profileName : "No instalado")
                        .font(.callout)
                        .fontWeight(.semibold)
                }
                if status.installed && status.recentFps != "N/A" {
                    Text("Rendimiento 3D: \(status.recentFps)")
                        .font(.caption)
                        .foregroundColor(.green)
                }
            }
            .padding(.horizontal, 12)
            .padding(.vertical, 8)
            .background(Color(NSColor.controlBackgroundColor).opacity(0.8))
            .cornerRadius(8)
        }
        .padding(.horizontal, 20)
        .padding(.vertical, 14)
        .background(Color(NSColor.windowBackgroundColor))
    }
}

struct InstallationCard: View {
    @ObservedObject var vm: LauncherViewModel

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack {
                Label("Estado del Juego", systemImage: "internaldrive.fill")
                    .font(.headline)
                Spacer()
                if vm.status.installed {
                    Button(action: { vm.showReextract.toggle() }) {
                        Label(vm.showReextract ? "Ocultar Extractor" : "Re-extraer ISO...", systemImage: "arrow.triangle.2.circlepath")
                            .font(.caption)
                    }
                    .buttonStyle(.borderless)
                }
            }

            // Game Root Directory Picker
            HStack {
                Image(systemName: "folder")
                    .foregroundColor(.secondary)
                Text("Carpeta del juego:")
                    .font(.caption)
                    .foregroundColor(.secondary)
                Text(vm.repoRoot.path)
                    .font(.caption)
                    .fontWeight(.medium)
                    .lineLimit(1)
                    .truncationMode(.middle)
                Spacer()
                Button("Cambiar...") {
                    vm.selectGameFolder()
                }
                .font(.caption)
            }
            .padding(8)
            .background(Color(NSColor.windowBackgroundColor).opacity(0.6))
            .cornerRadius(6)

            if !vm.status.installed || vm.showReextract {
                VStack(alignment: .leading, spacing: 10) {
                    Text("Selecciona el archivo ISO de Fable 2 (Xbox 360) para extraer los datos:")
                        .font(.subheadline)
                        .foregroundColor(.secondary)

                    HStack {
                        TextField("Ruta de la ISO...", text: $vm.selectedIsoPath)
                            .textFieldStyle(RoundedBorderTextFieldStyle())

                        Button("Examinar...") {
                            vm.selectIsoFile()
                        }

                        Button(vm.isExtracting ? "Extrayendo..." : (vm.status.installed ? "Re-extraer ISO" : "Extraer ISO")) {
                            vm.startExtraction()
                        }
                        .buttonStyle(.borderedProminent)
                        .disabled(vm.selectedIsoPath.isEmpty || vm.isExtracting)
                    }

                    if vm.isExtracting {
                        VStack(alignment: .leading, spacing: 4) {
                            ProgressView(value: vm.extractionPercent)
                                .progressViewStyle(LinearProgressViewStyle())
                            HStack {
                                Text(vm.extractionFile)
                                    .font(.caption)
                                    .lineLimit(1)
                                    .foregroundColor(.secondary)
                                Spacer()
                                Text("\(Int(vm.extractionPercent * 100))%")
                                    .font(.caption)
                                    .fontWeight(.bold)
                            }
                        }
                        .padding(.top, 4)
                    }

                    if let err = vm.extractionError {
                        HStack(alignment: .top, spacing: 8) {
                            Image(systemName: "exclamationmark.triangle.fill")
                                .foregroundColor(.red)
                            Text(err)
                                .font(.caption)
                                .foregroundColor(.red)
                                .textSelection(.enabled)
                        }
                        .padding(8)
                        .background(Color.red.opacity(0.1))
                        .cornerRadius(6)
                    }
                }
            }

            if vm.status.installed {
                HStack(spacing: 20) {
                    VStack(alignment: .leading, spacing: 3) {
                        Text("Edición verificada")
                            .font(.caption)
                            .foregroundColor(.secondary)
                        Text(vm.status.profileName)
                            .font(.subheadline)
                            .fontWeight(.medium)
                    }
                    Divider().frame(height: 28)
                    VStack(alignment: .leading, spacing: 3) {
                        Text("Partidas guardadas")
                            .font(.caption)
                            .foregroundColor(.secondary)
                        Text("\(vm.status.saveCount) perfiles")
                            .font(.subheadline)
                            .fontWeight(.medium)
                    }
                    Divider().frame(height: 28)
                    VStack(alignment: .leading, spacing: 3) {
                        Text("Ejecutable Recomp")
                            .font(.caption)
                            .foregroundColor(.secondary)
                        Text(vm.status.executablePresent ? "Compilado y listo" : "Falta compilar")
                            .font(.subheadline)
                            .fontWeight(.medium)
                            .foregroundColor(vm.status.executablePresent ? .primary : .red)
                    }
                    Spacer()
                }
            }
        }
        .padding(14)
        .background(Color(NSColor.controlBackgroundColor))
        .cornerRadius(10)
    }
}

struct PresetsCard: View {
    @Binding var selectedPreset: String
    let onSelect: (String) -> Void

    let presets = [
        ("enhanced", "Enhanced (Retina)", "Pantalla completa Retina a 30 FPS nativos"),
        ("balanced", "Balanced (1080p)", "Ventana 1080p a 30 FPS estables"),
        ("original", "Original (720p)", "Ventana 720p fiel a Xbox 360"),
        ("high-fps", "High Tick Rate", "60 FPS con parches 30/60 Hz de lógica")
    ]

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Label("Ajustes Predefinidos", systemImage: "sparkles")
                .font(.headline)

            HStack(spacing: 10) {
                ForEach(presets, id: \.0) { id, title, desc in
                    Button(action: { onSelect(id) }) {
                        VStack(alignment: .leading, spacing: 4) {
                            HStack {
                                Text(title)
                                    .fontWeight(.bold)
                                Spacer()
                                if selectedPreset == id {
                                    Image(systemName: "checkmark.circle.fill")
                                        .foregroundColor(.accentColor)
                                }
                            }
                            Text(desc)
                                .font(.caption2)
                                .foregroundColor(.secondary)
                                .multilineTextAlignment(.leading)
                        }
                        .padding(10)
                        .frame(maxWidth: .infinity, minHeight: 65, alignment: .topLeading)
                        .background(selectedPreset == id ? Color.accentColor.opacity(0.12) : Color(NSColor.controlBackgroundColor))
                        .overlay(
                            RoundedRectangle(cornerRadius: 8)
                                .stroke(selectedPreset == id ? Color.accentColor : Color.gray.opacity(0.2), lineWidth: 1.5)
                        )
                        .cornerRadius(8)
                    }
                    .buttonStyle(.plain)
                }
            }
        }
        .padding(14)
        .background(Color(NSColor.controlBackgroundColor))
        .cornerRadius(10)
    }
}

struct DisplaySettingsCard: View {
    @Binding var isFullscreen: Bool
    @Binding var selectedResolution: String

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Label("Pantalla y Resolución", systemImage: "display")
                .font(.headline)

            HStack(spacing: 24) {
                Picker("Resolución:", selection: $selectedResolution) {
                    Text("Retina Nativa (3024 × 1964)").tag("retina")
                    Text("1440p (2560 × 1440)").tag("1440p")
                    Text("1080p (1920 × 1080)").tag("1080p")
                    Text("720p (1280 × 720)").tag("720p")
                }
                .pickerStyle(.menu)

                Toggle("Pantalla Completa", isOn: $isFullscreen)
                    .toggleStyle(.checkbox)
            }
        }
        .padding(14)
        .background(Color(NSColor.controlBackgroundColor))
        .cornerRadius(10)
    }
}

struct PatchesCard: View {
    @ObservedObject var vm: LauncherViewModel

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Label("Parches y Opciones del Juego", systemImage: "slider.horizontal.3")
                .font(.headline)

            LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible())], spacing: 10) {
                Toggle("Saltar vídeos de inicio", isOn: $vm.skipIntroVideos)
                    .toggleStyle(.checkbox)
                Toggle("Desbloquear ítems de la web", isOn: $vm.unlockWebsite)
                    .toggleStyle(.checkbox)
                Toggle("Desbloquear Edición Coleccionista", isOn: $vm.unlockCe)
                    .toggleStyle(.checkbox)
                Toggle("Desactivar desenfoque de movimiento", isOn: $vm.disableMotionBlur)
                    .toggleStyle(.checkbox)
                Toggle("Morfología de texturas en GPU", isOn: $vm.realtimeTextureMorphing)
                    .toggleStyle(.checkbox)
                Toggle("Cámara libre con ratón (Mouse Look)", isOn: $vm.mouseLook)
                    .toggleStyle(.checkbox)
                Toggle("High Tick Rate (Física 30/60 Hz)", isOn: $vm.highTickRate)
                    .toggleStyle(.checkbox)
            }
        }
        .padding(14)
        .background(Color(NSColor.controlBackgroundColor))
        .cornerRadius(10)
    }
}

struct BottomBarView: View {
    @ObservedObject var vm: LauncherViewModel

    var body: some View {
        HStack(spacing: 12) {
            Button(action: { vm.openSavesFolder() }) {
                Label("Partidas", systemImage: "folder")
            }

            Button(action: { vm.openLogsFolder() }) {
                Label("Registros", systemImage: "doc.text")
            }

            Spacer()

            Button(action: { vm.saveConfiguration() }) {
                Text("Guardar Ajustes")
            }

            Button(action: { vm.launchGame() }) {
                HStack {
                    Image(systemName: "play.fill")
                    Text("Jugar a Fable II")
                        .fontWeight(.bold)
                }
                .padding(.horizontal, 10)
            }
            .buttonStyle(.borderedProminent)
            .controlSize(.large)
            .disabled(!vm.status.executablePresent)
        }
        .padding(.horizontal, 20)
        .padding(.vertical, 14)
        .background(Color(NSColor.windowBackgroundColor))
    }
}

// MARK: - App Entry Point

@main
struct Fable2LauncherApp: App {
    var body: some Scene {
        WindowGroup {
            ContentView()
        }
        .windowStyle(HiddenTitleBarWindowStyle())
        .windowResizability(.contentSize)
    }
}
