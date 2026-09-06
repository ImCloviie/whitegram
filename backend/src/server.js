import cors from 'cors';
import express from 'express';
import multer from 'multer';
import path from 'node:path';
import {
	config,
	ensureDataDirs,
	paths,
} from './config.js';
import { createE2eeKeysRouter } from './routes/e2eeKeys.js';
import { createProfileBackgroundRouter } from './routes/profileBackgrounds.js';
import { sendClientInfo } from './routes/clientInfo.js';
import { sendUpdateManifest } from './routes/updates.js';

ensureDataDirs();

const app = express();
app.locals.apiKey = config.apiKey;
app.locals.profileBackgroundsDir = paths.profileBackgrounds;

app.use(cors());
app.use(express.json({ limit: '1mb' }));

app.get('/health', (_req, res) => {
	res.json({ ok: true, service: 'whitegram-backend' });
});

app.get('/current', sendUpdateManifest);
app.get('/current:version(\\d+)', sendUpdateManifest);
app.get('/api/v1/client-info', sendClientInfo);

app.use('/updates', express.static(paths.updates, {
	fallthrough: false,
	setHeaders(res, filePath) {
		if (filePath.endsWith('.exe')) {
			res.setHeader('Content-Type', 'application/octet-stream');
		}
	},
}));

app.use(
	'/api/v1/profile-backgrounds',
	createProfileBackgroundRouter(),
);

app.use(
	'/api/v1/e2ee/keys',
	createE2eeKeysRouter(),
);

app.use((err, _req, res, _next) => {
	if (err instanceof multer.MulterError) {
		return res.status(400).json({ error: err.message });
	}
	return res.status(500).json({ error: 'Internal server error' });
});

const port = config.port;
const host = config.host;

app.listen(port, host, () => {
	console.log(`Whitegram backend listening on http://${host}:${port}`);
	console.log(`Updates manifest: ${path.resolve(paths.updateManifest)}`);
	console.log(`Profile backgrounds: ${path.resolve(paths.profileBackgrounds)}`);
});
