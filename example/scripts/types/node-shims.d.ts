declare module "node:assert" {
	const nodeAssert: any;
	export default nodeAssert;
}

declare module "node:assert/strict" {
	const nodeAssert: any;
	export default nodeAssert;
}

declare module "node:crypto" {
	const crypto: any;
	export default crypto;
}

declare module "node:fs" {
	const fs: any;
	export default fs;
}

declare module "node:http" {
	const http: any;
	export default http;
}

declare module "node:os" {
	const os: any;
	export default os;
}

declare module "node:path" {
	const path: any;
	export default path;
}

declare module "node:querystring" {
	const querystring: any;
	export default querystring;
}

declare module "node:util" {
	const util: any;
	export default util;
}

declare module "node:v8" {
	const v8: any;
	export default v8;
}

declare module "node:vm" {
	const vm: any;
	export default vm;
}

declare module "node:zlib" {
	const zlib: any;
	export default zlib;
}

declare module "node:events" {
	export const EventEmitter: any;
}

declare module "node:stream" {
	export const Readable: any;
	export const Transform: any;
}

declare module "node:url" {
	export const URL: any;
	export const fileURLToPath: any;
	export const pathToFileURL: any;
}

declare module "*.cjs" {
	const defaultExport: any;
	export default defaultExport;
	export const makeCommonPayload: any;
}

declare const Buffer: any;
declare const process: any;
