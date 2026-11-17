import fs from "node:fs";
import path from "node:path";
import {createRequire} from "node:module";
import {fileURLToPath} from "node:url";

const directory = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const arguments_ = process.argv.slice(2);

function argument(name, fallback) {
    const index = arguments_.indexOf(name);
    if (index < 0) return fallback;
    if (arguments_[index + 1] === undefined) throw new Error(`Missing value for ${name}`);
    return path.resolve(arguments_[index + 1]);
}

const npmDirectory = argument("--npm-dir", directory);
const dependencyRoot = fs.realpathSync(path.join(npmDirectory, "node_modules"));
const require = createRequire(path.join(npmDirectory, "package.json"));
const ts = require("typescript");
const esbuild = require("esbuild");
const root = fs.realpathSync(argument("--source", path.join(directory, "premade")));
const outdir = argument("--outdir", path.join(directory, "dist"));
const config = argument("--config", path.join(directory, "tsconfig.json"));
const watching = arguments_.includes("--watch");
const allModules = arguments_.includes("--all-modules");
const analyzing = arguments_.includes("--analyze");
const manifestPath = path.join(outdir, ".idhan-bundles.json");

function inside(parent, file) {
    const relative = path.relative(parent, file);
    return relative !== "" && !relative.startsWith(`..${path.sep}`) && relative !== ".." && !path.isAbsolute(relative);
}

if (inside(root, outdir) || root === outdir) throw new Error("Bundle output must be outside the source directory");

function diagnostics(diagnostics_) {
    if (diagnostics_.length === 0) return false;
    console.error(ts.formatDiagnosticsWithColorAndContext(diagnostics_, {
        getCanonicalFileName: name => name,
        getCurrentDirectory: () => directory,
        getNewLine: () => "\n"
    }));
    return diagnostics_.some(item => item.category === ts.DiagnosticCategory.Error);
}

function atomicWrite(file, contents) {
    fs.mkdirSync(path.dirname(file), {recursive: true});
    if (fs.existsSync(file) && fs.readFileSync(file).equals(Buffer.from(contents))) return;
    const temporary = `${file}.${process.pid}.tmp`;
    try {
        fs.writeFileSync(temporary, contents);
        fs.renameSync(temporary, file);
    } finally {
        fs.rmSync(temporary, {force: true});
    }
}

async function build(program) {
    if (diagnostics(ts.getPreEmitDiagnostics(program))) {
        if (!watching) process.exitCode = 1;
        return;
    }

    const entries = [];
    for (const file of program.getRootFileNames()) {
        if (file.endsWith(".d.ts") || file.endsWith(".d.mts")) continue;
        const relative = path.relative(root, file);
        if (!inside(root, fs.realpathSync(file))) throw new Error(`Source is outside the parser directory: ${file}`);
        const shared = relative.split(path.sep).includes("packages");
        if (shared && !allModules && relative !== path.join("packages", "sdk", "discovery.ts")) continue;
        const manifest = path.join(path.dirname(file), "package.json");
        if (fs.existsSync(manifest)) {
            const metadata = JSON.parse(fs.readFileSync(manifest, "utf8"));
            if (metadata.idhanParser !== undefined) {
                if (path.resolve(path.dirname(file), metadata.idhanParser) !== path.resolve(file)) continue;
                entries.push({in: file, out: path.dirname(relative)});
                continue;
            }
        }
        entries.push({in: file, out: relative.slice(0, -path.extname(relative).length)});
    }

    // Complete every bundle in memory before publishing any output. Standard decorators
    // are lowered to ES2022; QuickJS never needs a TypeScript parser or a Node API.
    const result = await esbuild.build({
        entryPoints: entries,
        nodePaths: [dependencyRoot],
        bundle: true,
        minify: true,
        sourcemap: "linked",
        sourcesContent: true,
        treeShaking: true,
        metafile: true,
        legalComments: "eof",
        splitting: false,
        format: "esm",
        platform: "neutral",
        mainFields: ["module", "main"],
        target: "es2022",
        outdir,
        tsconfig: config,
        write: false,
        logLevel: "silent",
        plugins: [{
            name: "parser-root",
            setup(build_) {
                // Resolve npm imports from the installation selected by --npm-dir,
                // including dependencies installed within an individual workspace.
                build_.onResolve({filter: /^[^./]/}, args => {
                    if (args.pluginData?.npmResolved || path.isAbsolute(args.path) || args.kind === "entry-point") return undefined;
                    let resolveDir = npmDirectory;
                    if (inside(dependencyRoot, args.importer)) {
                        resolveDir = path.dirname(args.importer);
                    } else if (inside(root, args.importer)) {
                        const relative = path.relative(root, path.dirname(args.importer));
                        const manifest = path.join(npmDirectory, "premade", relative, "package.json");
                        if (fs.existsSync(manifest)) resolveDir = path.dirname(manifest);
                    }
                    return build_.resolve(args.path, {
                        kind: args.kind, resolveDir, pluginData: {npmResolved: true}
                    });
                });
                // Match every source path to check canonical containment.
                build_.onLoad({filter: /./}, args => {
                    const canonical = fs.realpathSync(args.path);
                    if (!inside(root, canonical) && !inside(dependencyRoot, canonical)) throw new Error(`Import is outside the parser directory: ${args.path}`);
                    return undefined;
                });
            }
        }]
    });

    const analysis = await esbuild.analyzeMetafile(result.metafile, {verbose: true});
    const previous = fs.existsSync(manifestPath) ? JSON.parse(fs.readFileSync(manifestPath, "utf8")) : [];
    const files = result.outputFiles.map(file => path.relative(outdir, file.path));
    for (const output of result.outputFiles) atomicWrite(output.path, output.contents);
    for (const old of previous) {
        const file = path.resolve(outdir, old);
        if (!inside(outdir, file)) throw new Error(`Invalid bundle manifest path: ${old}`);
        if (!files.includes(old)) fs.rmSync(file, {force: true});
    }
    atomicWrite(manifestPath, JSON.stringify(files.sort(), null, 2) + "\n");
    // Build reports stay outside the runtime bundle manifest and contain no source text.
    atomicWrite(path.join(outdir, ".idhan-metafile.json"), JSON.stringify(result.metafile, null, 2) + "\n");
    atomicWrite(path.join(outdir, ".idhan-analysis.txt"), analysis);
    if (analyzing) console.log(analysis);
    const bundleCount = files.filter(file => file.endsWith(".js")).length;
    console.log(`Built ${bundleCount} JavaScript bundles in ${outdir}`);
}

// CMake installs dependencies in its build directory; fixtures may also live outside
// the workspace. Resolve the same npm packages for TypeScript and esbuild.
const compilerOptions = {
    noEmit: true,
    baseUrl: npmDirectory,
    paths: {"*": ["node_modules/@types/*/index.d.ts", "node_modules/*"]}
};

if (watching) {
    let pending = Promise.resolve();
    const host = ts.createWatchCompilerHost(config, compilerOptions, ts.sys,
        ts.createSemanticDiagnosticsBuilderProgram,
        diagnostic => diagnostics([diagnostic]),
        diagnostic => console.log(ts.flattenDiagnosticMessageText(diagnostic.messageText, "\n")));
    host.afterProgramCreate = builder => {
        pending = pending.then(() => build(builder.getProgram())).catch(error => console.error(error));
    };
    ts.createWatchProgram(host);
} else {
    const parsed = ts.getParsedCommandLineOfConfigFile(config, compilerOptions, {
        ...ts.sys,
        onUnRecoverableConfigFileDiagnostic: diagnostic => {
            diagnostics([diagnostic]);
            process.exitCode = 1;
        }
    });
    if (parsed === undefined || diagnostics(parsed.errors)) {
        process.exitCode = 1;
    } else {
        try {
            await build(ts.createProgram(parsed.fileNames, parsed.options));
        } catch (error) {
            console.error(error);
            process.exitCode = 1;
        }
    }
}
