import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import {spawn, spawnSync} from "node:child_process";
import {setTimeout as delay} from "node:timers/promises";
import {fileURLToPath, pathToFileURL} from "node:url";
import {SourceMap} from "node:module";
import test from "node:test";

const directory = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const script = path.join(directory, "scripts/build-parsers.mjs");

function fixture(context) {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), "idhan-bundles-"));
    context.after(() => fs.rmSync(root, {recursive: true, force: true}));
    const source = path.join(root, "src");
    const output = path.join(root, "dist");
    fs.mkdirSync(path.join(source, "packages"), {recursive: true});
    fs.writeFileSync(path.join(source, "main.ts"), "import {value} from './packages/shared.js'; export const result = value;");
    fs.writeFileSync(path.join(source, "extra.ts"), "export const extra = 1;");
    const dependency = path.join(source, "packages/shared.ts");
    fs.writeFileSync(dependency, "export const value: number = 42;");
    const config = path.join(root, "tsconfig.json");
    fs.writeFileSync(config, JSON.stringify({
        compilerOptions: {target: "ES2022", module: "ESNext", moduleResolution: "Bundler", strict: true, noEmit: true},
        include: ["src/**/*.ts"]
    }));
    const args = [script, "--source", source, "--config", config, "--outdir", output];
    return {source, output, dependency, args};
}

function run(args) {
    return spawnSync(process.execPath, args, {encoding: "utf8"});
}

async function waitFor(predicate, output) {
    const deadline = Date.now() + 15000;
    while (!predicate()) {
        if (Date.now() > deadline) throw new Error(`Timed out waiting for compiler: ${output()}`);
        await delay(50);
    }
}

test("bundles contain dependencies, preserve good output on error and prune only owned files", async context => {
    const files = fixture(context);
    const built = run(files.args);
    assert.equal(built.status, 0, built.stderr);
    const output = path.join(files.output, "main.js");
    const module = await import(pathToFileURL(output));
    assert.equal(module.result, 42);
    assert.equal(fs.existsSync(path.join(files.output, "packages/shared.js")), false);
    const previous = fs.readFileSync(output, "utf8");
    const previousMap = fs.readFileSync(`${output}.map`, "utf8");
    fs.writeFileSync(files.dependency, 'export const value: number = "broken";');
    const failed = run(files.args);
    assert.notEqual(failed.status, 0);
    assert.match(failed.stderr, /TS2322/);
    assert.equal(fs.readFileSync(output, "utf8"), previous);
    assert.equal(fs.readFileSync(`${output}.map`, "utf8"), previousMap);
    fs.writeFileSync(files.dependency, "export const value: number = 7;");
    fs.writeFileSync(path.join(files.output, "user.js"), "export const custom = true;");
    fs.rmSync(path.join(files.source, "extra.ts"));
    const recovered = run(files.args);
    assert.equal(recovered.status, 0, recovered.stderr);
    assert.equal(fs.existsSync(path.join(files.output, "extra.js")), false);
    assert.equal(fs.existsSync(path.join(files.output, "extra.js.map")), false);
    assert.equal(fs.existsSync(path.join(files.output, "user.js")), true);
});

test("external watch mode rebuilds shared changes and recovers from type errors", async context => {
    const files = fixture(context);
    let log = "";
    const child = spawn(process.execPath, [...files.args, "--watch"], {stdio: ["ignore", "pipe", "pipe"]});
    child.stdout.on("data", chunk => {
        log += chunk;
    });
    child.stderr.on("data", chunk => {
        log += chunk;
    });
    context.after(async () => {
        child.kill();
        if (child.exitCode === null && child.signalCode === null) {
            await new Promise(resolve => child.once("exit", resolve));
        }
    });
    const count = () => log.split("JavaScript bundles in").length;
    await waitFor(() => count() >= 2, () => log);
    const output = path.join(files.output, "main.js");
    const first = fs.readFileSync(output, "utf8");
    fs.writeFileSync(files.dependency, "export const value: number = 17;");
    await waitFor(() => count() >= 3, () => log);
    const second = fs.readFileSync(output, "utf8");
    assert.notEqual(first, second);
    fs.writeFileSync(files.dependency, 'export const value: number = "invalid";');
    await waitFor(() => log.includes("TS2322"), () => log);
    assert.equal(fs.readFileSync(output, "utf8"), second);
    fs.writeFileSync(files.dependency, "export const value: number = 18;");
    await waitFor(() => count() >= 4, () => log);
    fs.rmSync(path.join(files.source, "extra.ts"));
    await waitFor(() => !fs.existsSync(path.join(files.output, "extra.js")), () => log);
});

test("parser packages bundle npm dependencies into stable runtime filenames", async context => {
    const files = fixture(context);
    const parser = path.join(files.source, "gelbooru");
    fs.mkdirSync(parser);
    fs.writeFileSync(path.join(parser, "package.json"), JSON.stringify({
        name: "@idhan/parser-gelbooru", type: "module", idhanParser: "index.ts",
        dependencies: {he: "2.0.0"}
    }));
    fs.writeFileSync(path.join(parser, "index.ts"),
        'import {decode} from "he"; export const source = decode("?mode=big&amp;illust_id=123");');
    const built = run(files.args);
    assert.equal(built.status, 0, built.stderr);
    const output = path.join(files.output, "gelbooru.js");
    const bundled = await import(pathToFileURL(output));
    assert.equal(bundled.source, "?mode=big&illust_id=123");
    assert.equal(fs.existsSync(path.join(files.output, "gelbooru/index.js")), false);
    assert.doesNotMatch(fs.readFileSync(output, "utf8"), /from ["']he["']/);
});

test("tree shaking removes unused dependency code while preserving exports and side effects", async context => {
    const files = fixture(context);
    fs.writeFileSync(files.dependency, `
        export const value = 42;
        export function unused() { return "IDHAN_UNUSED_SENTINEL"; }
        Reflect.set(globalThis, "idhanTreeShakingEffect", true);
    `);
    context.after(() => Reflect.deleteProperty(globalThis, "idhanTreeShakingEffect"));
    const built = run([...files.args, "--analyze"]);
    assert.equal(built.status, 0, built.stderr);
    const output = path.join(files.output, "main.js");
    assert.doesNotMatch(fs.readFileSync(output, "utf8"), /IDHAN_UNUSED_SENTINEL/);
    const bundled = await import(pathToFileURL(output));
    assert.equal(bundled.result, 42);
    assert.equal(Reflect.get(globalThis, "idhanTreeShakingEffect"), true);
    const metadata = JSON.parse(fs.readFileSync(path.join(files.output, ".idhan-metafile.json"), "utf8"));
    const entry = Object.values(metadata.outputs).find(output => output.entryPoint?.endsWith("main.ts"));
    assert.ok(entry.exports.includes("result"));
    assert.ok(Object.keys(entry.inputs).some(input => input.endsWith("packages/shared.ts")));
    assert.match(fs.readFileSync(path.join(files.output, ".idhan-analysis.txt"), "utf8"), /main\.js/);
    assert.match(built.stdout, /main\.js/);
    const manifest = JSON.parse(fs.readFileSync(path.join(files.output, ".idhan-bundles.json"), "utf8"));
    assert.ok(manifest.every(file => file.endsWith(".js") || file.endsWith(".js.map")));
    assert.ok(manifest.includes("main.js.map"));
});

test("linked source maps resolve minified dependency code to embedded TypeScript", context => {
    const files = fixture(context);
    const source = 'export function fail(): never {\n    throw new Error("IDHAN_SOURCE_MAP_TEST");\n}\n';
    fs.writeFileSync(files.dependency, source);
    fs.writeFileSync(path.join(files.source, "main.ts"), 'export {fail} from "./packages/shared.js";');
    const built = run(files.args);
    assert.equal(built.status, 0, built.stderr);
    const output = path.join(files.output, "main.js");
    const javascript = fs.readFileSync(output, "utf8");
    // Match the linked map comment, e.g. //# sourceMappingURL=main.js.map.
    assert.match(javascript, /\/\/# sourceMappingURL=main\.js\.map/);
    const payload = JSON.parse(fs.readFileSync(`${output}.map`, "utf8"));
    const sourceIndex = payload.sources.findIndex(file => file.endsWith("packages/shared.ts"));
    assert.ok(sourceIndex >= 0);
    assert.equal(payload.sourcesContent[sourceIndex], source);
    const generatedOffset = javascript.indexOf("throw");
    assert.ok(generatedOffset >= 0);
    const prefix = javascript.slice(0, generatedOffset).split("\n");
    const entry = new SourceMap(payload).findEntry(prefix.length - 1, prefix.at(-1).length);
    assert.ok(entry.originalSource.endsWith("packages/shared.ts"));
    assert.equal(entry.originalLine, 1);

    const destination = path.join(files.output, "..", "staged");
    const stageArgs = [
        `-DSOURCE=${files.source}`, `-DEMITTED=${files.output}`, `-DDESTINATION=${destination}`,
        `-DMANIFEST=${path.join(destination, ".staged-files")}`,
        "-P", path.join(directory, "cmake/StageParsers.cmake")
    ];
    const staged = spawnSync("cmake", stageArgs, {encoding: "utf8"});
    assert.equal(staged.status, 0, staged.stderr);
    assert.equal(fs.readFileSync(path.join(destination, "main.js.map"), "utf8"), fs.readFileSync(`${output}.map`, "utf8"));
    fs.writeFileSync(path.join(destination, "user.js.map"), "{}");
    fs.rmSync(path.join(files.source, "main.ts"));
    const rebuilt = run(files.args);
    assert.equal(rebuilt.status, 0, rebuilt.stderr);
    const restaged = spawnSync("cmake", stageArgs, {encoding: "utf8"});
    assert.equal(restaged.status, 0, restaged.stderr);
    assert.equal(fs.existsSync(path.join(destination, "main.js.map")), false);
    assert.equal(fs.existsSync(path.join(destination, "user.js.map")), true);
});
