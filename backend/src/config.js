import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import dotenv from 'dotenv';

dotenv.config();

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const rootDir = path.join(__dirname, '..');

export const config = {
	port: Number(process.env.PORT || 8787),
	host: process.env.HOST || '0.0.0.0',
	apiKey: process.env.WHITGRAM_API_KEY || 'change-me-in-production',
	dataDir: path.resolve(rootDir, process.env.DATA_DIR || './data'),
};

export const paths = {
	updates: path.join(config.dataDir, 'updates'),
	updateManifest: path.join(config.dataDir, 'updates', 'current.json'),
	clientInfo: path.join(config.dataDir, 'client-info.json'),
	profileBackgrounds: path.join(config.dataDir, 'profile-backgrounds'),
	profileIndex: path.join(config.dataDir, 'profile-backgrounds', 'index.json'),
	e2eeKeys: path.join(config.dataDir, 'e2ee-keys.json'),
};

export function ensureDataDirs() {
	for (const dir of [
		config.dataDir,
		paths.updates,
		paths.profileBackgrounds,
	]) {
		fs.mkdirSync(dir, { recursive: true });
	}
	if (!fs.existsSync(paths.updateManifest)) {
		const sample = {
			win64: {
				stable: {
					released: 7000009,
					link: '/updates/Whitegram-7000009.exe',
				},
			},
			win: {
				stable: {
					released: 7000009,
					link: '/updates/Whitegram-7000009.exe',
				},
			},
			winarm: {
				stable: {
					released: 7000009,
					link: '/updates/Whitegram-7000009.exe',
				},
			},
			mac: {
				stable: {
					released: 7000009,
					link: '/updates/Whitegram-7000009.zip',
				},
			},
			linux: {
				stable: {
					released: 7000009,
					link: '/updates/Whitegram-7000009.tar.xz',
				},
			},
		};
		fs.writeFileSync(
			paths.updateManifest,
			JSON.stringify(sample, null, 2),
			'utf8',
		);
	}
	if (!fs.existsSync(paths.profileIndex)) {
		fs.writeFileSync(paths.profileIndex, '{}', 'utf8');
	}
	if (!fs.existsSync(paths.clientInfo)) {
		const sample = {
			betaVersion: '1',
			betaLabel: 'beta',
		};
		fs.writeFileSync(
			paths.clientInfo,
			JSON.stringify(sample, null, 2),
			'utf8',
		);
	}
	if (!fs.existsSync(paths.e2eeKeys)) {
		fs.writeFileSync(paths.e2eeKeys, '{}', 'utf8');
	}
}

export function readE2eeKeys() {
	try {
		return JSON.parse(fs.readFileSync(paths.e2eeKeys, 'utf8'));
	} catch {
		return {};
	}
}

export function writeE2eeKeys(keys) {
	fs.writeFileSync(
		paths.e2eeKeys,
		JSON.stringify(keys, null, 2),
		'utf8',
	);
}

export function readProfileIndex() {
	try {
		return JSON.parse(fs.readFileSync(paths.profileIndex, 'utf8'));
	} catch {
		return {};
	}
}

export function writeProfileIndex(index) {
	fs.writeFileSync(
		paths.profileIndex,
		JSON.stringify(index, null, 2),
		'utf8',
	);
}

export function zoneKey(zone) {
	const normalized = String(zone || '').toLowerCase();
	if (!['top', 'bottom', 'full'].includes(normalized)) {
		return null;
	}
	return normalized;
}

export function backgroundFilePath(userId, zone, ext) {
	return path.join(
		paths.profileBackgrounds,
		String(userId),
		`${zone}.${ext}`,
	);
}
