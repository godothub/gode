"use strict";

(function() {
	const fs = require("fs");
	const path = require("path");

	let ts = null;

	function getTypescript() {
		if (ts) {
			return ts;
		}
		ts = require("res://addons/gode/tsc/lib/typescript.js");
		return ts;
	}

	function normalizePath(filePath) {
		return String(filePath || "").replace(/\\/g, "/");
	}

	function isRelativeModuleName(moduleName) {
		return moduleName.startsWith("./") || moduleName.startsWith("../");
	}

	function resourceDirname(filePath) {
		const normalized = normalizePath(filePath);
		for (const prefix of ["res://", "user://"]) {
			if (normalized === prefix) {
				return "";
			}
			if (normalized.startsWith(prefix)) {
				const body = normalized.slice(prefix.length).replace(/\/+$/g, "");
				const slash = body.lastIndexOf("/");
				return slash >= 0 ? `${prefix}${body.slice(0, slash)}` : prefix;
			}
		}
		const slash = normalized.lastIndexOf("/");
		return slash >= 0 ? normalized.slice(0, slash) : "";
	}

	function normalizeResourcePath(filePath) {
		const normalized = normalizePath(filePath);
		const hasResourcePrefix = normalized.startsWith("res://");
		if (normalized.includes("://") && !hasResourcePrefix) {
			return "";
		}
		const prefix = hasResourcePrefix ? "res://" : "";
		const body = hasResourcePrefix ? normalized.slice("res://".length) : normalized;
		const segments = [];
		for (const part of body.split("/")) {
			if (!part || part === ".") {
				continue;
			}
			if (part === "..") {
				if (segments.length === 0) {
					return "";
				}
				segments.pop();
				continue;
			}
			segments.push(part);
		}
		return prefix + segments.join("/");
	}

	function sourceModuleBasePath(moduleName, containingFile) {
		if (moduleName.startsWith("res://")) {
			return normalizeResourcePath(moduleName);
		}
		if (!isRelativeModuleName(moduleName)) {
			return "";
		}
		return normalizeResourcePath(`${resourceDirname(containingFile)}/${moduleName}`);
	}

	function pushUnique(array, value) {
		if (!array.includes(value)) {
			array.push(value);
		}
	}

	function sourceModuleCandidates(moduleName, containingFile) {
		const base = sourceModuleBasePath(moduleName, containingFile);
		if (!base) {
			return [];
		}
		return sourcePathCandidatesForBase(base);
	}

	function sourcePathCandidatesForBase(base) {
		if (!base) {
			return [];
		}
		const candidates = [];
		const lower = base.toLowerCase();
		const runtimeExtensions = [".js", ".jsx", ".mjs", ".cjs"];
		for (const extension of runtimeExtensions) {
			if (lower.endsWith(extension)) {
				const stem = base.slice(0, -extension.length);
				if (extension === ".jsx") {
					pushUnique(candidates, `${stem}.tsx`);
					pushUnique(candidates, `${stem}.ts`);
				} else {
					pushUnique(candidates, `${stem}.ts`);
					pushUnique(candidates, `${stem}.tsx`);
				}
				pushUnique(candidates, `${stem}.d.ts`);
				pushUnique(candidates, base);
				return candidates;
			}
		}

		if (lower.endsWith(".ts") || lower.endsWith(".tsx") || lower.endsWith(".d.ts")) {
			pushUnique(candidates, base);
			return candidates;
		}

		pushUnique(candidates, `${base}.ts`);
		pushUnique(candidates, `${base}.tsx`);
		pushUnique(candidates, `${base}.d.ts`);
		pushUnique(candidates, `${base}/index.ts`);
		pushUnique(candidates, `${base}/index.tsx`);
		pushUnique(candidates, `${base}/index.d.ts`);
		return candidates;
	}

	function configPathBase(pathValue) {
		const normalized = normalizePath(pathValue);
		if (!normalized || normalized === ".") {
			return "res://";
		}
		if (normalized.startsWith("res://")) {
			return normalizeResourcePath(normalized);
		}
		return normalizeResourcePath(`res://${normalized}`);
	}

	function patternPrefixLength(pattern) {
		const star = pattern.indexOf("*");
		return star === -1 ? pattern.length : star;
	}

	function matchPathPattern(pattern, moduleName) {
		const star = pattern.indexOf("*");
		if (star === -1) {
			return pattern === moduleName ? "" : null;
		}
		const prefix = pattern.slice(0, star);
		const suffix = pattern.slice(star + 1);
		if (!moduleName.startsWith(prefix) || !moduleName.endsWith(suffix)) {
			return null;
		}
		return moduleName.slice(prefix.length, moduleName.length - suffix.length);
	}

	function configuredModuleCandidates(moduleName, config) {
		const rawOptions = config.rawCompilerOptions || {};
		const candidates = [];
		const baseUrl = typeof rawOptions.baseUrl === "string" && rawOptions.baseUrl
			? configPathBase(rawOptions.baseUrl)
			: "res://";
		if (!baseUrl) {
			return candidates;
		}
		const paths = rawOptions.paths && typeof rawOptions.paths === "object" ? rawOptions.paths : {};
		const patterns = Object.keys(paths).sort((left, right) => patternPrefixLength(right) - patternPrefixLength(left));

		for (const pattern of patterns) {
			const matched = matchPathPattern(pattern, moduleName);
			if (matched === null) {
				continue;
			}
			const targets = Array.isArray(paths[pattern]) ? paths[pattern] : [];
			for (const target of targets) {
				const targetText = String(target);
				const mapped = targetText.includes("*") ? targetText.replace("*", matched) : targetText;
				const mappedBase = normalizeResourcePath(`${baseUrl}/${mapped}`);
				for (const candidate of sourcePathCandidatesForBase(mappedBase)) {
					pushUnique(candidates, candidate);
				}
			}
		}

		if (typeof rawOptions.baseUrl === "string" && rawOptions.baseUrl) {
			for (const candidate of sourcePathCandidatesForBase(normalizeResourcePath(`${baseUrl}/${moduleName}`))) {
				pushUnique(candidates, candidate);
			}
		}
		return candidates;
	}

	function resolveProjectModule(tsApi, baseHost, moduleName, containingFile, config) {
		const sourceCandidates = isRelativeModuleName(moduleName) || moduleName.startsWith("res://")
			? sourceModuleCandidates(moduleName, containingFile)
			: configuredModuleCandidates(moduleName, config);
		for (const candidate of sourceCandidates) {
			if (!baseHost.fileExists(candidate)) {
				continue;
			}
			return {
				resolvedFileName: candidate,
				extension: sourceFileExtension(tsApi, candidate),
				isExternalLibraryImport: candidate.includes("/node_modules/")
			};
		}
		return undefined;
	}

	function sourceFileExtension(tsApi, filePath) {
		const lower = normalizePath(filePath).toLowerCase();
		if (lower.endsWith(".d.ts")) {
			return tsApi.Extension.Dts;
		}
		if (lower.endsWith(".tsx")) {
			return tsApi.Extension.Tsx;
		}
		if (lower.endsWith(".ts")) {
			return tsApi.Extension.Ts;
		}
		if (lower.endsWith(".jsx")) {
			return tsApi.Extension.Jsx;
		}
		return tsApi.Extension.Js;
	}

	function parentDirs(filePath) {
		const dirs = [];
		let dir = resourceDirname(filePath);
		while (dir && !dirs.includes(dir)) {
			dirs.push(dir);
			if (dir === "res://" || dir === "user://") {
				break;
			}
			const parent = resourceDirname(dir);
			if (parent === dir) {
				break;
			}
			dir = parent;
		}
		return dirs;
	}

	function createSourceContext(sources) {
		const sourceMap = new Map();
		const directories = new Set(["res://"]);

		for (const source of sources) {
			const filePath = normalizePath(source.path);
			sourceMap.set(filePath, String(source.source || ""));
			for (const dir of parentDirs(filePath)) {
				directories.add(dir);
			}
		}

		return {
			sourceMap,
			sourceFiles: Array.from(sourceMap.keys()).sort(),
			directories
		};
	}

	function normalizeDirectoryPath(directoryName) {
		let normalized = normalizePath(directoryName);
		while (normalized.length > "res://".length && normalized.endsWith("/")) {
			normalized = normalized.slice(0, -1);
		}
		return normalized;
	}

	function toTypescriptVirtualPath(filePath) {
		const normalized = normalizePath(filePath);
		if (normalized === "res://") {
			return "/__gode_res__";
		}
		if (normalized.startsWith("res://")) {
			return `/__gode_res__/${normalized.slice("res://".length)}`;
		}
		return normalized;
	}

	function fromTypescriptVirtualPath(filePath) {
		const normalized = normalizePath(filePath);
		if (normalized === "/__gode_res__") {
			return "res://";
		}
		if (normalized.startsWith("/__gode_res__/")) {
			return `res://${normalized.slice("/__gode_res__/".length)}`;
		}
		return normalized;
	}

	function isUnderDirectory(filePath, directoryName) {
		const normalizedDirectory = normalizeDirectoryPath(directoryName);
		if (normalizedDirectory === "res://") {
			return filePath.startsWith("res://");
		}
		return filePath === normalizedDirectory || filePath.startsWith(`${normalizedDirectory}/`);
	}

	function matchesExtension(filePath, extensions) {
		if (!Array.isArray(extensions) || extensions.length === 0) {
			return true;
		}
		const lower = filePath.toLowerCase();
		return extensions.some((extension) => lower.endsWith(String(extension).toLowerCase()));
	}

	function normalizeTypescriptVirtualDirectoryPath(directoryName) {
		let normalized = normalizePath(directoryName);
		while (normalized.length > 1 && normalized.endsWith("/")) {
			normalized = normalized.slice(0, -1);
		}
		return normalized;
	}

	function fileSystemEntriesForVirtualDirectory(virtualSourceFiles, directoryName) {
		const directory = normalizeTypescriptVirtualDirectoryPath(directoryName);
		const prefix = directory === "/__gode_res__" ? "/__gode_res__/" : `${directory}/`;
		const files = [];
		const directories = new Set();

		for (const filePath of virtualSourceFiles) {
			if (!filePath.startsWith(prefix)) {
				continue;
			}
			const relative = filePath.slice(prefix.length);
			if (!relative) {
				continue;
			}
			const slash = relative.indexOf("/");
			if (slash === -1) {
				files.push(relative);
			} else {
				directories.add(relative.slice(0, slash));
			}
		}

		return {
			files,
			directories: Array.from(directories)
		};
	}

	function readDirectoryWithTypescript(tsApi, context, directoryName, extensions, excludes, includes, depth) {
		if (tsApi && typeof tsApi.matchFiles === "function") {
			const virtualSourceFiles = context.sourceFiles.map((filePath) => toTypescriptVirtualPath(filePath)).sort();
			const virtualDirectory = toTypescriptVirtualPath(normalizeDirectoryPath(directoryName));
			return tsApi.matchFiles(
				virtualDirectory,
				extensions,
				excludes,
				includes,
				true,
				toTypescriptVirtualPath("res://"),
				depth,
				(directory) => fileSystemEntriesForVirtualDirectory(virtualSourceFiles, directory),
				(filePath) => normalizePath(filePath)
			).map((filePath) => fromTypescriptVirtualPath(filePath));
		}

		return context.sourceFiles.filter((filePath) => {
			return isUnderDirectory(filePath, directoryName) && matchesExtension(filePath, extensions);
		});
	}

	function categoryName(tsApi, category) {
		switch (category) {
			case tsApi.DiagnosticCategory.Warning:
				return "warning";
			case tsApi.DiagnosticCategory.Message:
				return "message";
			case tsApi.DiagnosticCategory.Suggestion:
				return "suggestion";
			default:
				return "error";
		}
	}

	function diagnosticToObject(tsApi, diagnostic) {
		const file = diagnostic.file;
		const position = file && typeof diagnostic.start === "number"
			? file.getLineAndCharacterOfPosition(diagnostic.start)
			: null;
		return {
			category: categoryName(tsApi, diagnostic.category),
			code: diagnostic.code || 0,
			message: tsApi.flattenDiagnosticMessageText(diagnostic.messageText, "\n"),
			file: file ? normalizePath(file.fileName) : "",
			line: position ? position.line + 1 : 0,
			column: position ? position.character + 1 : 0
		};
	}

	function readTsConfig(tsApi, host) {
		const configPath = "res://tsconfig.json";
		if (!fs.existsSync(configPath)) {
			return { options: {}, rawCompilerOptions: {}, diagnostics: [], fileNames: [], hasConfig: false };
		}

		const config = tsApi.readConfigFile(configPath, host.readFile);
		if (config.error) {
			return { options: {}, rawCompilerOptions: {}, diagnostics: [config.error], fileNames: [], hasConfig: true };
		}

		const rawCompilerOptions = {
			ignoreDeprecations: "6.0",
			...((config.config && config.config.compilerOptions) || {})
		};
		const configObject = {
			...(config.config || {}),
			compilerOptions: rawCompilerOptions
		};
		const parsed = tsApi.parseJsonConfigFileContent(
			configObject,
			{
					useCaseSensitiveFileNames: true,
					fileExists: host.fileExists,
					readFile: host.readFile,
					readDirectory: host.readDirectory,
					getCurrentDirectory: () => "res://",
					onUnRecoverableConfigFileDiagnostic: () => {}
				},
			"res://"
		);

		return {
			options: parsed.options || {},
			rawCompilerOptions,
			diagnostics: parsed.errors || [],
			fileNames: (parsed.fileNames || []).map((fileName) => normalizePath(fileName)).sort(),
			hasConfig: true
		};
	}

	function createBaseHost(context, tsApi) {
		const readFile = (fileName) => {
			const normalized = normalizePath(fileName);
			if (context.sourceMap.has(normalized)) {
				return context.sourceMap.get(normalized);
			}
			if (!fs.existsSync(normalized)) {
				return undefined;
			}
			return fs.readFileSync(normalized, "utf8");
		};

		const fileExists = (fileName) => {
			const normalized = normalizePath(fileName);
			return context.sourceMap.has(normalized) || fs.existsSync(normalized);
		};

		const directoryExists = (directoryName) => {
			const normalized = normalizeDirectoryPath(directoryName);
			if (context.directories.has(normalized)) {
				return true;
			}
			return fs.existsSync(normalized) && fs.statSync(normalized).isDirectory();
		};

		const readDirectory = (directoryName, extensions, excludes, includes, depth) => {
			return readDirectoryWithTypescript(tsApi, context, directoryName, extensions, excludes, includes, depth);
		};

		return {
			fileExists,
			readFile,
			directoryExists,
			readDirectory
		};
	}

	function createHost(tsApi, context, options, config) {
		const baseHost = createBaseHost(context, tsApi);
		const host = tsApi.createCompilerHost(options, true);
		host.useCaseSensitiveFileNames = () => true;
		host.getCurrentDirectory = () => "res://";
		host.getCanonicalFileName = (fileName) => normalizePath(fileName);
		host.getNewLine = () => "\n";
		host.fileExists = baseHost.fileExists;
		host.readFile = baseHost.readFile;
		host.directoryExists = baseHost.directoryExists;
		host.readDirectory = baseHost.readDirectory;
		host.getDirectories = () => [];
		host.writeFile = () => {};
		host.getSourceFile = (fileName, languageVersion) => {
			const source = baseHost.readFile(fileName);
			if (source === undefined) {
				return undefined;
			}
			return tsApi.createSourceFile(normalizePath(fileName), source, languageVersion, true);
		};
		host.resolveModuleNames = (moduleNames, containingFile) => moduleNames.map((moduleName) => {
			if (moduleName === "godot") {
				return {
					resolvedFileName: "res://addons/gode/types/godot.d.ts",
					extension: tsApi.Extension.Dts,
					isExternalLibraryImport: false
				};
			}
			const sourceResolved = resolveProjectModule(tsApi, baseHost, moduleName, normalizePath(containingFile), config);
			if (sourceResolved) {
				return sourceResolved;
			}
			const resolved = tsApi.resolveModuleName(
				moduleName,
				normalizePath(containingFile),
				options,
				host
			).resolvedModule;
			return resolved || undefined;
		});

		return host;
	}

	function defaultCompilerOptions(tsApi) {
		return {
			target: tsApi.ScriptTarget.ES2022,
			module: tsApi.ModuleKind.ESNext,
			moduleResolution: tsApi.ModuleResolutionKind.Bundler || tsApi.ModuleResolutionKind.Node10,
			jsx: tsApi.JsxEmit.React,
			strict: true,
			isolatedModules: true,
			forceConsistentCasingInFileNames: true,
			useDefineForClassFields: true,
			esModuleInterop: true,
			allowSyntheticDefaultImports: true,
			experimentalDecorators: true,
			skipLibCheck: true,
			types: [],
			ignoreDeprecations: "6.0",
			inlineSourceMap: true,
			inlineSources: true,
			declaration: false,
			noEmit: true
		};
	}

	function sourceRootForSource(filePath) {
		const normalized = normalizePath(filePath);
		const slash = normalized.lastIndexOf("/");
		if (slash < 0) {
			return "";
		}
		for (const prefix of ["res://", "user://"]) {
			if (normalized.startsWith(prefix) && slash < prefix.length) {
				return prefix;
			}
		}
		return normalized.slice(0, slash + 1);
	}

	function isEmittableSource(filePath) {
		const normalized = normalizePath(filePath).toLowerCase();
		return (normalized.endsWith(".ts") || normalized.endsWith(".tsx")) && !normalized.endsWith(".d.ts");
	}

	function sourceToOutputPath(filePath) {
		const normalized = normalizeResourcePath(filePath);
		return normalized.replace(/\.tsx?$/i, ".js");
	}

	function resourcePathBody(filePath) {
		const normalized = normalizeResourcePath(filePath);
		return normalized.startsWith("res://") ? normalized.slice("res://".length) : normalized;
	}

	function relativeOutputSpecifier(fromSourcePath, toSourcePath) {
		const fromDir = path.posix.dirname(resourcePathBody(fromSourcePath));
		const targetPath = resourcePathBody(sourceToOutputPath(toSourcePath));
		let relative = path.posix.relative(fromDir === "." ? "" : fromDir, targetPath).replace(/\\/g, "/");
		if (!relative.startsWith(".")) {
			relative = `./${relative}`;
		}
		return relative;
	}

	function isStringLiteralNode(tsApi, node) {
		return node && (node.kind === tsApi.SyntaxKind.StringLiteral || node.kind === tsApi.SyntaxKind.NoSubstitutionTemplateLiteral);
	}

	function rewrittenProjectModuleSpecifier(tsApi, baseHost, specifierText, sourcePath, config) {
		const resolved = resolveProjectModule(tsApi, baseHost, specifierText, sourcePath, config);
		if (!resolved || !isEmittableSource(resolved.resolvedFileName)) {
			return "";
		}
		const replacement = relativeOutputSpecifier(sourcePath, resolved.resolvedFileName);
		return replacement === specifierText ? "" : replacement;
	}

	function createProjectModuleSpecifierTransformer(tsApi, sourcePath, context, config) {
		const baseHost = createBaseHost(context, tsApi);

		return (transformContext) => {
			const factory = transformContext.factory || tsApi.factory;

			function rewriteModuleSpecifier(specifierNode) {
				if (!isStringLiteralNode(tsApi, specifierNode)) {
					return specifierNode;
				}
				const replacement = rewrittenProjectModuleSpecifier(tsApi, baseHost, specifierNode.text, sourcePath, config);
				return replacement ? factory.createStringLiteral(replacement) : specifierNode;
			}

			function visit(node) {
				if (tsApi.isImportDeclaration(node) && node.moduleSpecifier) {
					const moduleSpecifier = rewriteModuleSpecifier(node.moduleSpecifier);
					if (moduleSpecifier !== node.moduleSpecifier) {
						return factory.updateImportDeclaration(
								node,
								node.modifiers,
								node.importClause,
								moduleSpecifier,
								node.attributes || node.assertClause);
					}
				}

				if (tsApi.isExportDeclaration(node) && node.moduleSpecifier) {
					const moduleSpecifier = rewriteModuleSpecifier(node.moduleSpecifier);
					if (moduleSpecifier !== node.moduleSpecifier) {
						return factory.updateExportDeclaration(
								node,
								node.modifiers,
								node.isTypeOnly,
								node.exportClause,
								moduleSpecifier,
								node.attributes || node.assertClause);
					}
				}

				if (tsApi.isCallExpression(node) &&
						node.expression.kind === tsApi.SyntaxKind.ImportKeyword &&
						node.arguments.length > 0) {
					const moduleSpecifier = rewriteModuleSpecifier(node.arguments[0]);
					if (moduleSpecifier !== node.arguments[0]) {
						const args = [moduleSpecifier, ...Array.prototype.slice.call(node.arguments, 1)];
						return factory.updateCallExpression(node, node.expression, node.typeArguments, args);
					}
				}
				return tsApi.visitEachChild(node, visit, transformContext);
			}

			return (sourceFile) => tsApi.visitNode(sourceFile, visit);
		};
	}

	function projectRootNames(config, sources) {
		if (config.hasConfig) {
			return config.fileNames.slice();
		}
		return sources.map((source) => normalizePath(source.path));
	}

	function emittableProgramSources(program, context) {
		const outputs = [];
		for (const sourceFile of program.getSourceFiles()) {
			const sourcePath = normalizePath(sourceFile.fileName);
			if (!context.sourceMap.has(sourcePath) || !isEmittableSource(sourcePath)) {
				continue;
			}
			outputs.push({
				path: sourcePath,
				source: context.sourceMap.get(sourcePath)
			});
		}
		outputs.sort((left, right) => left.path.localeCompare(right.path));
		return outputs;
	}

	globalThis.__gode_compile_typescript_project = function(files) {
		try {
			const tsApi = getTypescript();
			const sources = Array.isArray(files) ? files : [];
			const context = createSourceContext(sources);
			const baseHost = createBaseHost(context, tsApi);
			const config = readTsConfig(tsApi, baseHost);
			const compilerOptions = {
				...defaultCompilerOptions(tsApi),
				...config.options,
				module: tsApi.ModuleKind.ESNext,
				moduleResolution: tsApi.ModuleResolutionKind.Bundler || tsApi.ModuleResolutionKind.Node10,
				noEmit: true,
				sourceMap: false,
				inlineSourceMap: true,
				inlineSources: true,
				declaration: false,
				allowImportingTsExtensions: false
			};
			const host = createHost(tsApi, context, compilerOptions, config);
			const rootNames = projectRootNames(config, sources);
			for (const internalType of ["res://addons/gode/types/globals.d.ts"]) {
				if (fs.existsSync(internalType) && !rootNames.includes(internalType)) {
					rootNames.push(internalType);
				}
			}

			const program = tsApi.createProgram(rootNames, compilerOptions, host);
			const diagnostics = [...config.diagnostics, ...tsApi.getPreEmitDiagnostics(program)];
			const emitOptions = {
				...compilerOptions,
				noEmit: false,
				noEmitOnError: false
			};
			const outputs = [];

			for (const source of emittableProgramSources(program, context)) {
				const sourcePath = source.path;
				const sourceEmitOptions = {
					...emitOptions,
					sourceRoot: sourceRootForSource(sourcePath)
				};
				const result = tsApi.transpileModule(String(source.source || ""), {
					compilerOptions: sourceEmitOptions,
					fileName: sourcePath,
					reportDiagnostics: true,
					transformers: {
						before: [createProjectModuleSpecifierTransformer(tsApi, sourcePath, context, config)]
					}
				});
				outputs.push({
					source: sourcePath,
					code: result.outputText || "",
					sourceMap: result.sourceMapText || ""
				});
				if (result.diagnostics) {
					diagnostics.push(...result.diagnostics);
				}
			}

			const diagnosticsOut = diagnostics.map((diagnostic) => diagnosticToObject(tsApi, diagnostic));
			const hasErrors = diagnostics.some((diagnostic) => diagnostic.category === tsApi.DiagnosticCategory.Error);
			return {
				ok: !hasErrors,
				outputs,
				diagnostics: diagnosticsOut
			};
		} catch (error) {
			return {
				ok: false,
				outputs: [],
				diagnostics: [{
					category: "error",
					code: 0,
					message: error && (error.stack || error.message) || String(error),
					file: "",
					line: 0,
					column: 0
				}]
			};
		}
};
})();
